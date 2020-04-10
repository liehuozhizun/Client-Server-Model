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

#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <utility>
#include <map>
#include <list>
using namespace std;

#define BUFFER_SIZE 4096 /* 4 KB maximum buffer size */

void *processing(void *arg);
int32_t *thread_state;
int32_t nreaders;
//int32_t thread_in_use = 0;
pthread_t *thread;
sem_t dispatcher;
//pthread_mutex_t dplock;
pthread_mutex_t rwlock;
sem_t writing;
//pthread_mutex_t writing;


/*
// struct for cache: hash <key>
struct key_model {
	char key_httpname[41];
	ssize_t key_block_number;
};

// LRUCache basic structure
class LRUCache {
	list<string> cache;
	unordered_map<struct key_model, list<string>::iterator> cache_hash;
	size_t block_number;
	
	public:
		LRUCache(size_t n);
		uint32_t refer(void *info);
		void update(void *info);
};

LRUCache::LRUCache(size_t n){
	block_number = n;
}

uint32_t LRUCache::refer(void *arg){
	struct key_model *key = (key_model *)arg;
	char *httpname = key->key_httpname;
	size_t real_block_number = key->key_block_number;
	// not present in cache
	if(cache_hash.find(key) == cache_hash.end()){
		return 0;
	}
	// present in cache
	else{
		// move relevent cache blocks to the front
		list<string> cache_tem;
		//list_t::iterator iter;
		auto iter = cache_hash[key];
		for(int i = 1; i <= real_block_number; i++){
			cache_tem.push_back((*iter));
			auto iter2 = cache_hash[key];
			cache.erase(iter2);
			iter ++;
		}
		cache.splice(cache.begin(), cache_tem);
		return 1;
	}
}
*/

std::list<std::string> cache;
typedef std::pair<std::string, ssize_t> key;
//auto k = std::make_pair<std::string, ssize_t>(httpname, block_nr);
std::map<key, std::list<std::string>> cache_hash;
std::map<key, std::list<std::string>>::iterator it;
//m[k]



// struct for thread_info passed into thread
struct thread_info {
	int32_t id;
	int32_t client;
};

int32_t main(int32_t argc, char *argv[]) {
	char address[33];
	int32_t port_number, opt;
	size_t thread_number = 4, cache_number = 40;

	// no arguments -> error
	if(argc == 1){
		fprintf(stderr, "SET UP FAILED: no valid arguments\n");
		exit(EXIT_FAILURE);
	}
	
	// obtain -N and -c option values
	while((opt = getopt(argc, argv, "N:c:")) != -1){
		switch(opt){
		case 'N':
			thread_number = atoi(optarg);
			if(thread_number <= 0){ // check validation of -N (must > 0)
				fprintf(stderr, "SET UP FAILED: Thread Number must be greater than 0\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'c':
			cache_number = atoi(optarg);
			if(thread_number <= 0){ // check validation of -c (must > 0)
				fprintf(stderr, "SET UP FAILED: Cache Number must be greater than 0\n");
				exit(EXIT_FAILURE);
			}
			break;
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
	thread_state = new int32_t[thread_number];
	for(int j = 0; j<thread_number; j++){thread_state[j] = 0;}
	thread = new pthread_t[thread_number];
	sem_init(&dispatcher, 0, thread_number);
	sem_init(&writing, 0, 1);
	//pthread_mutex_init(&dlock, NULL);
	pthread_mutex_init(&rwlock, NULL);
	//pthread_mutex_init(&writing, NULL);
	struct thread_info tinfo[thread_number];
	
	// Thread Dispatcher
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
		
		// dispatch thread
		//printf("\n--- sem_wait(&dispatcher) ---\n");
		sem_wait(&dispatcher);
		//printf("\n--- dispatech: %d---\n", dispatcher);
		/*
		if(thread_in_use >= thread_number){
			pthread_mutex_lock(&dlock);
		}*/
		
		// look for available thread
		int32_t i = 0;
		for(i; i < thread_number; i++){
			printf("\nthread_state[%d] = %d\n", i, thread_state[i]);
			if(thread_state[i] == 0){
				printf("\n--- get into thread: %d ---\n", i);
				thread_state[i] = 1;
				//thread_in_use += 1;
				break;
			}
		}
		
		tinfo[i].id = i;
		tinfo[i].client = cl;
		
		int rc1;
		if( (rc1=pthread_create( &thread[i], NULL, processing, &tinfo[i])) ){
			fprintf(stderr, "Thread creation failed: %d\n", rc1);
		}
		//printf("\n--- finish sprawn thread: %d ---\n--- result: %d ---\n", i, rc1);
		//pthread_join(thread[i], NULL);
	}
	return 0;
}

void *processing(void *arg){
	// unpack passing arguments
	struct thread_info *info = (thread_info *)arg;
	int32_t cl = info->client;
	printf("\ncl: %d\n", cl);
	
	// initialization
	char buf[BUFFER_SIZE], file_buf[BUFFER_SIZE];
	int32_t fd = 0, create = 0;
	size_t file_size = 1, count = 0;
	ssize_t read_result, recv_result, write_result;
	char action[5], httpname[42], version[42];
	char *ptr, *ptr_tem;

	// receive request header
	if ((recv_result = recv(cl, buf, sizeof(buf) - 1, 0)) == -1) {
		fprintf(stderr, "%s\n", strerror(errno));
	}
	
	// obtain the action code, httpname, version
	if ((ptr = strstr(buf, " ")) == NULL) {
		send(cl, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0);
		close(cl);
		thread_state[(info->id)] = 0;
		//thread_in_use -= 1;
		sem_post(&dispatcher);//pthread_mutex_unlock(&dlock);
		return (NULL);
	}
	strncpy(action, buf, ptr - buf); // obtain action code
	size_t tem_count = 0;
	while (ptr[tem_count] == '/' || ptr[tem_count] == ' ') {
		tem_count++;
	}
	if ((ptr_tem = strstr(ptr + tem_count, " ")) == NULL) {
		send(cl, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0);
		close(cl);
		thread_state[(info->id)] = 0;
		//thread_in_use -= 1;
		sem_post(&dispatcher);//pthread_mutex_unlock(&dlock);
		return (NULL);
	}
	strncpy(httpname, ptr + tem_count,ptr_tem - (ptr + tem_count)); // obtain httpname
	if ((ptr = strstr(ptr_tem + 1, "\r\n")) == NULL) {
		send(cl, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0);
		close(cl);
		thread_state[(info->id)] = 0;
		//thread_in_use -= 1;
		sem_post(&dispatcher);//pthread_mutex_unlock(&dlock);
		return (NULL);
	}
	strncpy(version, ptr_tem + 1, ptr - (ptr_tem + 1)); // obtain version

	if (strstr(buf, "\r\n\r\n") == NULL) {
		send(cl, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0);
		close(cl);
		thread_state[(info->id)] = 0;
		//thread_in_use -= 1;
		sem_post(&dispatcher);//pthread_mutex_unlock(&dlock);
		return (NULL);
	}
	
	// check validation of httpname and version
	if ((strlen(httpname) != 40) || (strcmp(version, "HTTP/1.1") != 0)) {
		send(cl, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0);
		close(cl);
		thread_state[(info->id)] = 0;
		//thread_in_use -= 1;
		sem_post(&dispatcher);//pthread_mutex_unlock(&dlock);
		return (NULL);
	}
	fprintf(stdout, "\nRequest: %s %s %s\n", action, httpname, version);
	
	// read Content-Length from header
	if ((ptr_tem = strstr(buf, "Length: ")) != NULL) {
		strcpy(file_buf, ptr_tem);
		ptr_tem = strstr(file_buf, "\r\n");
		strncpy(file_buf, file_buf + 8, ptr_tem - file_buf - 8);
		file_size = atoi(file_buf);
		memset(file_buf, 0, sizeof(file_buf));
	}

	// PUT request
	if (strcmp(action, "PUT") == 0) {
		sem_wait(&writing);
		// check validation of httpfile
		if ((fd = open(httpname, O_WRONLY | O_TRUNC)) == -1) {
			if ((fd = open(httpname, O_CREAT | O_WRONLY | O_TRUNC)) == -1) {
				send(cl, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0);
				fprintf(stdout, "HTTP/1.1 400 Bad Request\n");
				close(fd);
				close(cl);
				thread_state[(info->id)] = 0;
				//thread_in_use -= 1;
				sem_post(&dispatcher);//pthread_mutex_unlock(&dlock);
				return (NULL);
			} else {
				create = 1;
			}
		} else {
			create = 0;
		}
		// write data to check httpfile validation
		if ((write_result = write(fd, 0, 0)) == -1) {
			send(cl, "HTTP/1.1 403 Forbidden\r\n\r\n", 26, 0);
			fprintf(stdout, "HTTP/1.1 403 Forbidden\n");
			close(fd);
			close(cl);
			thread_state[(info->id)] = 0;
			//thread_in_use -= 1;
			sem_post(&dispatcher);//pthread_mutex_unlock(&dlock);
			return (NULL);
		} else {
			if (create) {
				send(cl, "HTTP/1.1 201 Created\r\n\r\n", 24, 0);
				fprintf(stdout, "HTTP/1.1 201 Created\n");
			} else {
				send(cl, "HTTP/1.1 200 OK\r\n\r\n", 19, 0);
				fprintf(stdout, "HTTP/1.1 200 OK\n");
			}
		}

		// obtain file data
		ptr = strstr(buf, "\r\n\r\n");
		if (recv_result != (ptr - buf + 4)) { // means received file data as well
			strncpy(file_buf, ptr + 4, recv_result - (ptr - buf) - 4);
		} else { // means only received request header
			recv_result = recv(cl, file_buf, sizeof(file_buf) - 1, 0);
		}

		
		
		// write data to httpfile
		write_result = pwrite(fd, file_buf, strlen(file_buf), NULL);
		size_t write_offset = 0 + write_result;
		count += write_result;
		if (count == file_size) {
			close(fd);
			close(cl);
			thread_state[(info->id)] = 0;
			//thread_in_use -= 1;
			sem_post(&dispatcher);//pthread_mutex_unlock(&dlock);
			sem_post(&writing);
			return (NULL);
		}

		// receive the rest of file data
		while ((recv_result = recv(cl, file_buf, sizeof(file_buf) - 1, 0)) != 0) {
			write_result = pwrite(fd, file_buf, recv_result, write_offset);
			write_offset += write_result;
			count += write_result;
			if (count == file_size) {
				break;
			}
			memset(file_buf, 0, sizeof(file_buf));
		}
		// reset semaphore: writing
		sem_post(&writing);
	}

	// GET request
	else if (strcmp(action, "GET") == 0) {
	
		// change rw-lock
		pthread_mutex_lock(&rwlock);
		nreaders += 1;
		if(nreaders == 1){
			sem_wait(&writing);
		}
		pthread_mutex_unlock(&rwlock);
		
		
		//std::string fuck = (std::string)httpname;
		size_t real_block_number = 0;
		auto k = std::make_pair<std::string, ssize_t>((std::string)httpname, real_block_number);
		
/*		
		// initialization
		cache.refer();
		struct key_model *key_get;
		key_get.key_httpname = httpname;
		key_get.key_block_number = (ssize_t)();
		char *httpname = key->key_httpname;
		size_t real_block_number = key->key_block_number;
	
		//typedef std::pair<std::string, ssize_t> key;
		//auto k = std::make_pair<std::string, ssize_t>(httpname, block_nr);
		//std::map<key, block *> cache_hash;
		//m[k]

		struct stat st;
		stat(httpname, &st);
		size_t block_nr = ceil(st.st_size / BUFFER_SIZE);
		auto k = std::make_pair<std::string, ssize_t>(httpname, block_nr);
		// data not present in cache -> update the cache
		if((it = cache_hash.find(k)) == cache_hash.end()){
			;
		}
		
		// send response: status code
		send(cl, "HTTP/1.1 200 OK\r\n", 17, 0);
		fprintf(stdout, "HTTP/1.1 200 OK\n");
		// send response: Content-Length
		memset(buf, 0, sizeof(buf));
		strcat(buf, "Content-Length: ");
		char tem[sizeof(int32_t) * 8 + 1];
		sprintf(tem, "%zu", st.st_size);
		strcat(buf, tem);
		strcat(buf, "\r\n\r\n");
		send(cl, buf, strlen(buf), 0);
		
		// send data
		it = cache_hash.begin();
		for(int i = 1; i<= block_nr; i++){
			memset(file_buf, 0, sizeof(file_buf));
			std::string tem_buf == *it;
			send(cl, tem_buf, sizeof(tem_buf), 0);
			it ++;
		}
		
	
		send(cl, file_buf, read_result, 0);
				do {
					memset(file_buf, 0, sizeof(file_buf));
					if ((read_result = pread(fd, file_buf, sizeof(file_buf), read_offset)) != 0) {
						send(cl, file_buf, read_result, 0);
					}
					read_offset += read_result;
				} while (read_result != 0);
		
*/		
		// check httpfile validation
		if ((fd = open(httpname, O_RDONLY)) == -1) {
			send(cl, "HTTP/1.1 404 Not Found\r\n\r\n", 26, 0);
			fprintf(stdout, "HTTP/1.1 404 Not Found\n");
		} else {
			// read file data for first time 
			size_t read_offset = 0;
			if ((read_result = 
			(fd, file_buf, sizeof(file_buf), NULL)) == -1) {
				if (errno == 21) {
					send(cl, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0);
					fprintf(stdout, "HTTP/1.1 400 Bad Request\n");
				} else if (errno == 13) {
					send(cl, "HTTP/1.1 403 Forbidden\r\n\r\n", 26, 0);
					fprintf(stdout, "HTTP/1.1 403 Forbidden\n");
				}
			} else {
				read_offset += read_result;
				// send response: status code
				send(cl, "HTTP/1.1 200 OK\r\n", 17, 0);
				fprintf(stdout, "HTTP/1.1 200 OK\n");
				// send response: Content-Length
				memset(buf, 0, sizeof(buf));
				struct stat st;
				stat(httpname, &st);
				file_size = st.st_size;
				strcat(buf, "Content-Length: ");
				char tem[sizeof(int32_t) * 8 + 1];
				sprintf(tem, "%zu", file_size);
				strcat(buf, tem);
				strcat(buf, "\r\n\r\n");
				send(cl, buf, strlen(buf), 0);

				// send data
				send(cl, file_buf, read_result, 0);
				do {
					memset(file_buf, 0, sizeof(file_buf));
					if ((read_result = pread(fd, file_buf, sizeof(file_buf), read_offset)) != 0) {
						send(cl, file_buf, read_result, 0);
					}
					read_offset += read_result;
				} while (read_result != 0);
			}
		}
		// reset rw-lock
		pthread_mutex_lock(&rwlock);
		nreaders -= 1;
		if(nreaders == 0){
			sem_post(&writing);
		}
		pthread_mutex_unlock(&rwlock);
	}

	// other -> bad request
	else {
		send(cl, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0);
		fprintf(stdout, "HTTP/1.1 400 Bad Request\n");
	}
	close(fd);
	close(cl);
	
	thread_state[(info->id)] = 0;
	//thread_in_use -= 1;
	//pthread_mutex_unlock(&dlock);
	sem_post(&dispatcher);
	return (NULL);
}