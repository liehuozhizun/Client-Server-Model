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

#include <pthread.h>
#include <semaphore.h>

#define BUFFER_SIZE 4096 /* 4 KB maximum buffer size */

// multi-thread initialization
void *processing(void *arg);
int32_t nreaders;
pthread_t *thread;
sem_t dispatcher;
pthread_mutex_t dlock;
pthread_mutex_t rwlock;
pthread_cond_t busy_lock;
sem_t writing;

// define struct for thread_info passed into thread
struct thread_info {
	int32_t id;
	int32_t client;
	pthread_cond_t busy_lock;
};

int32_t main(int32_t argc, char *argv[]) {
	char address[33];
	int32_t port_number = 80, opt;
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
				fprintf(stderr, "%s\n", "SET UP FAILED: Thread Number cannot be less than or equal to 0");
				exit(EXIT_FAILURE);
			}
			break;
		case 'c':
			cache_number = atoi(optarg);
			if(thread_number < 0){ // check validation of -c (must >= 0)
				fprintf(stderr, "%s\n", "SET UP FAILED: Cache Number cannot be less than or equal to 0");
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
	thread = new pthread_t[thread_number];
	sem_init(&dispatcher, 0, thread_number);
	sem_init(&writing, 0, 1);
	pthread_mutex_init(&rwlock, NULL);
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
		pthread_mutex_lock(&dlock);
		if (info->client == -1){
			pthread_cond_wait(&info->busy_lock, &dlock);
		}
		pthread_mutex_unlock(&dlock);
		
		// initialization
		char buf[BUFFER_SIZE], file_buf[BUFFER_SIZE];
		int32_t fd = 0, create = 0;
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
		while (ptr[tem_count] == '/' || ptr[tem_count] == ' ') {
			tem_count++;
		}
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

		if (strstr(buf, "\r\n\r\n") == NULL) {
			send(cl, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0);
			close(cl);
			pthread_mutex_lock(&dlock);
			info->client = -1;
			sem_post(&dispatcher);
			pthread_mutex_unlock(&dlock);
			continue;
		}
		
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
					sem_post(&writing);
				pthread_mutex_lock(&dlock);
				info->client = -1;
				sem_post(&dispatcher);
				pthread_mutex_unlock(&dlock);
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
				sem_post(&writing);
				pthread_mutex_lock(&dlock);
				info->client = -1;
				sem_post(&dispatcher);
				pthread_mutex_unlock(&dlock);
				continue;
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
				sem_post(&writing);
				pthread_mutex_lock(&dlock);
				info->client = -1;
				sem_post(&dispatcher);
				pthread_mutex_unlock(&dlock);
				continue;
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

			// check httpfile validation
			if ((fd = open(httpname, O_RDONLY)) == -1) {
				send(cl, "HTTP/1.1 404 Not Found\r\n\r\n", 26, 0);
				fprintf(stdout, "HTTP/1.1 404 Not Found\n");
			} else {
				// read file data for first time 
				size_t read_offset = 0;
				if ((read_result = pread(fd, file_buf, sizeof(file_buf), read_offset)) == -1) {
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
						//write(1, file_buf, sizeof(file_buf));
						//printf("\nread_offset: %zu\nread_result: %d\n",read_offset, read_result);
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
		
		// reset working thread state
		pthread_mutex_lock(&dlock);
		info->client = -1;
		sem_post(&dispatcher);
		pthread_mutex_unlock(&dlock);
	}
}