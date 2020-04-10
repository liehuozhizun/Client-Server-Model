/*
 * Hang Yuan
 * 1564348, hyuan3
 *
 * ASGN 2: Multi-threaded HTTP Server with In-memory Caching
 * This program implements the basic functions of a HTTP server,
 * with multi-thread and caching functions, which can handle
 * multiple requests simutaneously.
 * Except for two new features, performance of basic function for
 * handling request is enhanced. Details are in DESIGN.pdf.
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

#include <pthread.h>
#include <semaphore.h>
#include <map>
using namespace std;

#define BUFFER_SIZE 4096 /* 4 KB maximum buffer size */
#define KVS_DELIMITER 280 // KVS delimiter between entry and data: 28*entry num
// #define KVS_ENTRY_NUMBER 10 // KVS entry number

// multi-thread initialization
void *processing(void *arg);
// int32_t nreaders;
pthread_t *thread;
sem_t dispatcher;
pthread_mutex_t dlock;
pthread_mutex_t rwlock;
pthread_cond_t busy_lock;
// sem_t writing;
sem_t kvs_end_lock;
sem_t kvs_entry_lock;

int32_t fd_kvs;
uint32_t kvs_end = KVS_DELIMITER;
uint32_t kvs_entry = 0;


ssize_t kvwrite(const uint8_t * object_name, size_t length, size_t offset, const char * data);
ssize_t kvread (const uint8_t * object_name, size_t length, size_t offset, char * data);
ssize_t kvinfo (const uint8_t * object_name, ssize_t length);
void kvs_init(bool status);
void name_converter (uint8_t * object_name, char * httpname);

// define struct for thread_info passed into thread
struct thread_info {
	int32_t id;
	int32_t client;
	pthread_cond_t busy_lock;
};

struct entry {
	uint8_t name[20];
	uint32_t pointer;
	uint32_t length;
};

// struct ptrCmp{
//     bool operator()( const char * s1, const char * s2 ) const{
//         return memcmp( s1, s2, 20) == 0;
//     }
// };

map<string, uint32_t> kvs_map;

int32_t main(int32_t argc, char *argv[]) {
	char address[33], doc_name[50];
	int32_t port_number = 80, opt;
	ssize_t thread_number = 4, cache_number = 40;

	// no arguments -> error
	if(argc == 1){
		fprintf(stderr, "SET UP FAILED: no valid arguments\n");
		exit(EXIT_FAILURE);
	}

	// obtain -N and -c option values
	while((opt = getopt(argc, argv, "N:c:f:")) != -1){
		switch(opt){
		case 'N':
			thread_number = atoi(optarg);
			if(thread_number <= 0){ // check validation of -N (must > 0)
				fprintf(stderr, "SET UP FAILED: Thread Number must be positive\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'c':
			cache_number = atoi(optarg);
			if(cache_number < 0){ // check validation of -c (must >= 0)
				fprintf(stderr, "SET UP FAILED: Cache Number must not be negetive\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'f':
			strncpy(doc_name, optarg, strlen(optarg));
			if((fd_kvs = open(doc_name, O_RDWR)) == -1){
				if((fd_kvs = open(doc_name, O_CREAT | O_RDWR | O_TRUNC)) == -1){
					fprintf(stderr, "SET UP FAILED: cannot open or creat the KVS file\n");
					exit(EXIT_FAILURE);
				} kvs_init(1); // new kvs initialization
			}	kvs_init(0); // refetch kvs initialization
		}
	}

	// Obtain the address and port number
	char port[8], *ptr;
	sscanf(argv[optind], "%[^:]", address); // obatin address
	if((ptr = strstr(argv[optind], ":")) == NULL){
		fprintf(stderr, "SET UP FAILED: not valid address and port number\n");
		exit(EXIT_FAILURE);
	}
	sscanf(ptr+1, "%s", port); // obtain port_number
	if ((port_number = atoi(port)) == 0) {
		port_number = 80;
	}

	printf("thread: %zu\ncache: %zu\naddress: %s\nport number: %d\n",thread_number, cache_number, address, port_number );

	// set up connection to client
	struct hostent *hent = gethostbyname(address);
	if (hent == NULL) {
		fprintf(stderr, "%s\n", strerror(h_errno));
		exit(EXIT_FAILURE);
	}
	struct sockaddr_in addr;
	memcpy(&addr.sin_addr.s_addr, hent->h_addr, hent->h_length);
	addr.sin_port = htons(port_number);
	addr.sin_family = AF_INET;
	int32_t sock = socket(AF_INET, SOCK_STREAM, 0);
	int32_t enable = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		fprintf(stderr, "%s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	// thread management initialization
	thread = new pthread_t[thread_number];
	sem_init(&dispatcher, 0, thread_number);
	// sem_init(&writing, 0, 1);
	sem_init(&kvs_end_lock, 0, 1);
	sem_init(&kvs_entry_lock, 0, 1);
	// pthread_mutex_init(&rwlock, NULL);
	struct thread_info tinfo[thread_number];

	// pre-initialize working thread
	for(int32_t i = 0; i < (int32_t)thread_number; i++){
		tinfo[i].id = i;
		tinfo[i].client = -1;
		pthread_cond_init(&tinfo[i].busy_lock, NULL);
		pthread_create(&thread[i], NULL, processing, &tinfo[i]);
	}

	// thread dispatcher
	for (;;) {
		// accept connection request
		int32_t cl;
		if (listen(sock, 0) == -1) {
			fprintf(stderr, "%s\n", strerror(errno));
			continue;
		}
		if ((cl = accept(sock, NULL, NULL)) == -1) {
			fprintf(stderr, "%s\n", strerror(errno));
			close(cl);
			continue;
		}

		// wake up available working thread
		sem_wait(&dispatcher);
		pthread_mutex_lock(&dlock);
		for(int32_t i = 0; i < (int32_t)thread_number; i++){
			if(tinfo[i].client == -1){
				tinfo[i].client = cl;
				pthread_cond_signal(&tinfo[i].busy_lock);
				break;
			}
		}
		pthread_mutex_unlock(&dlock);
	}
	return 0;
}

void *processing(void *arg){
	struct thread_info *info = (thread_info *)arg;
	// infinite loop
	for(;;){
		// pthread_mutex_lock(&dlock);
		if (info->client == -1){
			pthread_cond_wait(&info->busy_lock, &dlock);
		}
		// pthread_mutex_unlock(&dlock);

		printf("> start processing\n");
		// initialization
		char buf[BUFFER_SIZE], file_buf[BUFFER_SIZE];
		int32_t fd = 0;
		size_t file_size = 1, count = 0;
		ssize_t read_result, recv_result, write_result;
		char action[5], httpname[42], version[42];
		char *ptr, *ptr_tem;
		int32_t cl = info->client;

		// receive request header
		if ((recv_result = recv(cl, buf, sizeof(buf) - 1, 0)) == -1) {
			fprintf(stderr, "%s\n", strerror(errno));
		}

		// obtain the action code, httpname, version
		if ((ptr = strstr(buf, " ")) == NULL) {
			send(cl, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0);
			close(cl);
			pthread_mutex_lock(&dlock);
			info->client = -1;
			sem_post(&dispatcher);
			pthread_mutex_unlock(&dlock);
			continue;
		}
		strncpy(action, buf, ptr - buf); // obtain action code
		size_t tem_count = 0;
		// while (ptr[tem_count] == '/' || ptr[tem_count] == ' ') {
		// 	tem_count++;
		// }
		if ((ptr_tem = strstr(ptr + tem_count, " ")) == NULL) {
			send(cl, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0);
			close(cl);
			pthread_mutex_lock(&dlock);
			info->client = -1;
			sem_post(&dispatcher);
			pthread_mutex_unlock(&dlock);
			continue;
		}
		strncpy(httpname, ptr + tem_count,ptr_tem - (ptr + tem_count)); // obtain httpname
		if ((ptr = strstr(ptr_tem + 1, "\r\n")) == NULL) {
			send(cl, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0);
			close(cl);
			pthread_mutex_lock(&dlock);
			info->client = -1;
			sem_post(&dispatcher);
			pthread_mutex_unlock(&dlock);
			continue;
		}
		strncpy(version, ptr_tem + 1, ptr - (ptr_tem + 1)); // obtain version

		// if (strstr(buf, "\r\n\r\n") == NULL) {
		// 	send(cl, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0);
		// 	close(cl);
		// 	pthread_mutex_lock(&dlock);
		// 	info->client = -1;
		// 	sem_post(&dispatcher);
		// 	pthread_mutex_unlock(&dlock);
		// 	continue;
		// }

		// check validation of httpname and version
		if ((strlen(httpname) != 40) || (strcmp(version, "HTTP/1.1") != 0)) {
			send(cl, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0);
			close(cl);
			pthread_mutex_lock(&dlock);
			info->client = -1;
			sem_post(&dispatcher);
			pthread_mutex_unlock(&dlock);
			continue;
		}
		fprintf(stdout, "\nRequest: %s %s %s\n", action, httpname, version);

		// read Content-Length from header
		if ((ptr_tem = strstr(buf, "Length: ")) != NULL) {
			strcpy(file_buf, ptr_tem);
			ptr_tem = strstr(file_buf, "\r\n");
			strncpy(file_buf, file_buf + 8, ptr_tem - file_buf - 8);
			file_size = atoi(file_buf);
			memset(file_buf, 0, sizeof(file_buf));
			// printf(">CL: %d\n", file_size);
		}

		// PUT request
		if (strcmp(action, "PUT") == 0) {
			// sem_wait(&writing);

			// obtain file data
			ptr = strstr(buf, "\r\n\r\n");
			if (recv_result != (ptr - buf + 4)) { // means received file data as well
				strncpy(file_buf, ptr + 4, recv_result - (ptr - buf) - 4);
				recv_result = recv_result - (ptr - buf) - 4;
			} else { // means only received request header
				recv_result = recv(cl, file_buf, sizeof(file_buf) - 1, 0);
			}
			// printf("> start writing data\n");
			// write data to httpfile
			uint8_t object_name[20];
			name_converter(object_name, httpname);
			// printf("> start kvinfo(), file_size: %d\n", file_size);
			uint32_t offset = kvinfo(object_name, file_size);
			// printf("> end kvinfo(), file_size: %d\n", file_size);
			write_result = pwrite(fd_kvs, file_buf, recv_result, offset);
			count += write_result;
			offset += write_result;
			do{
				memset(file_buf, 0, sizeof(file_buf));
				recv_result = recv(cl, file_buf, sizeof(file_buf) - 1, 0);
				write_result = pwrite(fd_kvs, file_buf, recv_result, offset);
				count += write_result;
				offset += write_result;
			}while((file_size > count) && (recv_result != 0));
			// printf("stop writing data\n");

			send(cl, "HTTP/1.1 200 OK\r\n\r\n", 19, 0);
			fprintf(stdout, "HTTP/1.1 200 OK\n");

			// reset semaphore: writing
			// sem_post(&writing);
		}

		// GET request
		else if (strcmp(action, "GET") == 0) {

			// change rw-lock
			// pthread_mutex_lock(&rwlock);
			// nreaders += 1;
			// if(nreaders == 1){
			// 	sem_wait(&writing);
			// }
			// pthread_mutex_unlock(&rwlock);

			// check file existence
			uint8_t object_name[20];
			name_converter(object_name, httpname);
			if((file_size = kvinfo(object_name, -1)) == -2){// not found file
				send(cl, "HTTP/1.1 404 Not Found\r\n\r\n", 26, 0);
				fprintf(stdout, "HTTP/1.1 404 Not Found\n");
			}else{
				// send response: status code
				// printf("> file_size: %d", file_size);
				send(cl, "HTTP/1.1 200 OK\r\n", 17, 0);
				// send response: Content-Length
				memset(buf, 0, sizeof(buf));
				strcat(buf, "Content-Length: ");
				char tem[sizeof(ssize_t) * 8 + 1];
				sprintf(tem, "%zu", file_size);
				strcat(buf, tem);
				strcat(buf, "\r\n\r\n");
				send(cl, buf, strlen(buf), 0);
				fprintf(stdout, "HTTP/1.1 200 OK Content-Length: %zu\n", file_size);

				// send data
				uint32_t offset = kvinfo(object_name, file_size);
				if(file_size < 4096){
					read_result = pread(fd_kvs, file_buf, file_size, offset);
					// printf("> Data Sent Out: %s\n", file_buf);
					send(cl, file_buf, read_result, 0);
				}else{
					size_t read_amount = 4096;
					while(count < file_size){
						read_result = kvread(object_name, read_amount, offset, file_buf);
						send(cl, file_buf, read_result, 0);
						offset += read_result;
						count += read_result;
						if((file_size - count) < 4096){
							read_amount = file_size - count;
						}
					}
				}
			}

			// reset rw-lock
			// pthread_mutex_lock(&rwlock);
			// nreaders -= 1;
			// if(nreaders == 0){
			// 	sem_post(&writing);
			// }
			// pthread_mutex_unlock(&rwlock);
		}

		// other -> bad request
		else {
			send(cl, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0);
			fprintf(stdout, "HTTP/1.1 400 Bad Request\n");
		}
		close(fd);
		close(cl);

		printf("> end of actions\n");
		// reset working thread state
		pthread_mutex_lock(&dlock);
		info->client = -1;
		sem_post(&dispatcher);
		pthread_mutex_unlock(&dlock);
		printf("> end of reset\n");
	}
}

void name_converter (uint8_t * object_name, char * httpname) {
		for(uint8_t i = 0; i < 20; i++){
		char temp[2];
		temp[0] = httpname[i*2];
		temp[1] = httpname[i*2+1];
		object_name[i] = (uint8_t)strtol(temp, 0, 16);
	}
}

void kvs_init(bool new_file) {
	if(new_file) { // new kvs file
		struct entry *empty_entry = new entry;
		memset(empty_entry, 0, 28);
		for(uint32_t pointer = 0; pointer < KVS_DELIMITER; pointer += 28){
			if(pwrite(fd_kvs, empty_entry, 28, pointer) == -1){
				fprintf(stderr, "SET UP FAILED: not a vaild KVS file\n");
				exit(EXIT_FAILURE);
			}
		}
	}else{ // esisting kvs file, refetch into cache
		struct entry *empty_entry = new entry;
		for(uint32_t pointer = 0; pointer < KVS_DELIMITER; pointer += 28){
			pread(fd_kvs, empty_entry, 28, pointer);
			char obj_name[21];
			obj_name[20] = '\0';
			memcpy(obj_name, empty_entry->name, 20);
			if(strcmp(obj_name, "") == 0){
				return;
			}
			kvs_entry += 28;
			kvs_map.insert(make_pair(obj_name, pointer));
		}
	}
}

ssize_t kvinfo (const uint8_t * object_name, ssize_t length) {
	struct entry *empty_entry = new entry;
	map<string, uint32_t>::iterator iter;
	int32_t found = 0;
	char obj_name[21];
	memset(obj_name, 0, sizeof(obj_name));
	memcpy(obj_name, object_name, 20);
	printf("obj_name: %s\n", obj_name);
	if((iter = kvs_map.find(obj_name)) != kvs_map.end()){
		found = 1;
		printf("> FOUND\n");
	}
	if(length == -1){ // return object length
		// if not found, return -2
		if(found == 0){
			return -2;
		}
		// if found, return length
		uint32_t pointer = iter->second;
		pread(fd_kvs, empty_entry, 28, pointer);
		printf("-----\n> name: %s\n> pointer: %d\n> length: %d\n-----\n",empty_entry->name, empty_entry->pointer, empty_entry->length);
		return empty_entry->length;
	}else{ // set new object length
		// if found, update length and return new pointer
		if (found){
			uint32_t pointer = iter->second;
			pread(fd_kvs, empty_entry, 28, pointer);
			if(empty_entry->length == length){
					return empty_entry->pointer;
			}else{
				empty_entry->length = length;
				sem_wait(&kvs_end_lock);
				empty_entry->pointer = kvs_end;
				kvs_end += length;
				sem_post(&kvs_end_lock);
				pwrite(fd_kvs, empty_entry, 28, pointer);
				return empty_entry->pointer;
			}
		}
		// if not found, create entry, update cache, and return new pointer
		else{
			memcpy(empty_entry->name, object_name, 20);
			empty_entry->length = length;
			sem_wait(&kvs_end_lock);
			empty_entry->pointer = kvs_end;
			kvs_end += length;
			sem_post(&kvs_end_lock);
			sem_wait(&kvs_entry_lock);
			pwrite(fd_kvs, empty_entry, 28, kvs_entry);
			kvs_map.insert(make_pair(obj_name, kvs_entry));
			kvs_entry += 28;
			sem_post(&kvs_entry_lock);
			// if((iter = kvs_map.find(obj_name)) != kvs_map.end()){
			// 	printf("> FOUND: %s\n", iter->first.c_str());
			// }
			return empty_entry->pointer;
		}
	}
	// printf("> start kvinfo() in kvinfo()\n> length: %zu\n", length);
	// map<char *, uint32_t>::iterator iter;
	// iter = kvs_map.begin();
	// printf("> start iteration\n");
	// int num = 0;
	// bool found = 0;
	// // while(iter != kvs_map.end()){
	// 	// if(memcmp(iter->first, object_name, 20) == 0){
	// 	// 	found = 1;
	// 	// 	printf("\n> kvinfo FOUND\n");
	// 	// 	break;
	// 	// }
	// 	// printf("\nobj_name: %s\nempty_entry_name: %s\n\n", obj_name, empty_entry->name);
	// // 	write(1, iter->first, 20);
	// // 	printf("\n");
	// // 	printf(">> iteration result: %s\n>> object_name result: %s\n", iter->first, (char *)object_name);
	// //
	// // 	iter ++;
	// // 	num ++;
	// // 	printf("> start next iteration\n");
	// // }
	// char obj_namek[20];
	// memcpy(obj_namek, object_name, 20);
	// printf("> obj_namek: %s\n", obj_namek);
	// if((iter = kvs_map.find(obj_namek)) == kvs_map.end()){
	// 	printf("没找到:\n");
	// }else{
	// 	printf("找到了: %s -- %s -- %d\n", obj_namek, iter->first, iter->second);
	// }
	// printf("> stop iteration, num: %d\n", num);
	// if(length == -1){ // return object length
	// 	// if not found, return -2
	// 	if(!found){
	// 		return -2;
	// 	}
	// 	// if found, return length
	// 	uint32_t pointer = iter->second;
	// 	struct entry *empty_entry = new entry;
	// 	pread(fd_kvs, empty_entry, 28, pointer);
	// 	return empty_entry->length;
	// }else{ // set new object length
	// 	struct entry *empty_entry = new entry;
	// 	// if found, update length and return new pointer
	// 	if (found){
	// 		printf("> start found process\n");
	// 		uint32_t pointer = iter->second;
	// 		pread(fd_kvs, empty_entry, 28, pointer);
	// 		empty_entry->length = length;
	// 		empty_entry->pointer = kvs_end;
	// 		kvs_end += (uint32_t)length;
	// 		pwrite(fd_kvs, empty_entry, 28, pointer);
	// 		return empty_entry->pointer;
	// 	}
	// 	// if not found, create entry, update cache, and return new pointer
	// 	else{
	// 		printf("> start not found process\n");
	// 		memcpy(empty_entry->name, object_name, 20);
	// 		empty_entry->pointer = kvs_end;
	// 		printf("> kvs_end: %d\n", kvs_end);
	// 		kvs_end += (uint32_t)length;
	// 		printf("> kvs_end after add: %d\n", kvs_end);
	// 		empty_entry->length = length;
	// 		printf("> kvs_entry: %d\n", kvs_entry);
	// 		pwrite(fd_kvs, empty_entry, 28, kvs_entry);
	// 		char obj_name[20];
	// 		memcpy(obj_name, object_name, 20);
	// 		kvs_map.insert(make_pair(obj_name, kvs_entry));
	// 		printf("> obj_name: %s\n> object_name: %s\n", obj_name, (char *)object_name);
	// 		if((iter = kvs_map.find(obj_name)) != kvs_map.end()){
	// 			printf("CNMCNMCNM\n");
	// 		}
	// 		printf("WTF:::::: %s -- %d\n", iter->first, iter->second);
	// 		kvs_entry += 28;
	// 		printf("> kvs_entry: %d\n", kvs_entry);
	// 		return empty_entry->pointer;
	// 	}
	//
	// }
}

ssize_t kvwrite(const uint8_t * object_name, size_t length, size_t offset, const char * data) {
	object_name = NULL;
	pwrite(fd_kvs, data, length, offset);
	return length;
}

ssize_t kvread (const uint8_t * object_name, size_t length, size_t offset, char * data) {
	object_name = NULL;
	pread(fd_kvs, data, length, offset);
	return length;
}
