/*
 * Hang Yuan
 * 1564348, hyuan3
 * 
 * ASGN 1: HTTP Client and Server
 * This program implements the basic functions of a HTTP server,
 * which can communicate with an HTTP client. Receiving requests 
 * from and sending responses to the HTTP client.
 * 
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/stat.h>

#define BUFFER_SIZE 4096 /* 4 KB maximum buffer size */

int32_t main(int32_t argc, char *argv[]){
	char buf[BUFFER_SIZE], file_buf[BUFFER_SIZE], address[32];
	int32_t port_number;
	char * ptr, * ptr_tem;
	
	if(argc != 2){
		fprintf(stderr, "%s\n", "SET UP FAILED: not valid address and port number");
		exit(EXIT_FAILURE);
	}
	
	// Obtain the address and port number
	if((ptr = strchr(argv[1], ':')) == NULL){
		fprintf(stderr, "%s\n", "SET UP FAILED: not valid address and port number");
		exit(EXIT_FAILURE);
	}
	strncpy(address, ptr+1, (strlen(argv[1])-(ptr - argv[1] +1)));
	for(size_t i = 0; i < strlen(address); i++){
		if('0' > address[i] || address[i] > '9'){
			fprintf(stderr, "%s\n", "SET UP FAILED: not valid address and port number");
			exit(EXIT_FAILURE);
		}
	}
	if((port_number = atoi(address)) == 0){port_number = 80;}
	strncpy(address, argv[1], (ptr - argv[1]));
	
	// set up connection to client
	struct hostent *hent = gethostbyname(address);
	if(hent == NULL){
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

	if(bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1){
		fprintf(stderr, "%s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	for(;;){
		// clean buffer
		memset(buf, 0, BUFFER_SIZE);
		memset(file_buf, 0, BUFFER_SIZE);
		
		// accept connection request
		int32_t fd = 0, file_size = 1, cl, count = 0, create = 0;
		ssize_t read_result, recv_result, write_result;
		char action[5]; memset(action, 0, sizeof(action));
		char httpname[42]; memset(httpname, 0, sizeof(httpname));
		char version[42]; memset(version, 0, sizeof(version));
		if(listen(sock, 0) == -1){
			fprintf(stderr, "%s\n", strerror(errno));
		}
		if((cl = accept(sock, NULL, NULL)) == -1){
			fprintf(stderr, "%s\n", strerror(errno));
		}
	
		// receive request header
		if((recv_result = recv(cl, buf, sizeof(buf)-1, MSG_WAITALL)) == -1){
			fprintf(stderr, "%s\n", strerror(errno));
		}
		
		// obtain the action code, httpname, version
		if((ptr = strstr(buf, " ")) == NULL){
			send(cl, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0);
			close(cl);
			continue;
		}
		strncpy(action, buf, ptr - buf); // obtain action code
		int32_t tem_count = 0;
		while(ptr[tem_count] == '/' || ptr[tem_count] == ' '){tem_count++;}
		strncpy(buf, ptr + tem_count, strlen(ptr + tem_count));
		if((ptr = strstr(buf, " ")) == NULL){
			send(cl, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0);
			close(cl);
			continue;
		}
		strncpy(httpname, buf, ptr - buf); // obtain httpname
		strncpy(buf, ptr + 1, strlen(ptr + 1));
		if((ptr = strstr(buf, "\r\n")) == NULL){
			send(cl, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0);
			close(cl);
			continue;
		}
		strncpy(version, buf, ptr - buf); // obtain version
		if(strstr(buf, "\r\n\r\n") == NULL){
			send(cl, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0);
			close(cl);
			continue;
		}
		
		// check validation of httpname and version
		if((strlen(httpname) != 40) || (strcmp(version, "HTTP/1.1") != 0)){
			send(cl, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0);
			close(cl);
			continue;
		}
		
		// read Content-Length from header
		if((ptr_tem = strstr(buf, "Length: ")) != NULL){
			strcpy(file_buf, ptr_tem);
			ptr_tem = strstr(file_buf, "\r\n");
			strncpy(file_buf, file_buf + 8, ptr_tem - file_buf - 8);
			file_size = atoi(file_buf);
		}
		
		// PUT request
		if(strcmp(action, "PUT") == 0){			
			// obtain file datagt
			ptr = strstr(buf, "\r\n\r\n");
			if(recv_result != (ptr - buf + 4)){ // means received file data as well
				strcpy(file_buf, ptr + 4);
			}else{ // means only received request header
				recv_result = recv(cl, file_buf, sizeof(file_buf)-1, 0);
			}
			
			// check validation of httpfile
			if((fd = open(httpname, O_WRONLY|O_TRUNC)) == -1){
				if((fd = open(httpname, O_CREAT|O_WRONLY|O_TRUNC)) == -1){
					send(cl, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0);
					close(fd);
					close(cl);
					continue;
				}else{create = 1;}
			}else{create = 0;}
			
			// write data to check httpfile validation
			if((write_result = write(fd, file_buf, strlen(file_buf))) == -1){
				send(cl, "HTTP/1.1 403 Forbidden\r\n\r\n", 26, 0);
			}else{
				count += write_result;
				//send 200/201 response
				if(create){
					send(cl, "HTTP/1.1 201 Created\r\n\r\n", 24, 0);
				}else{
					send(cl, "HTTP/1.1 200 OK\r\n\r\n", 19, 0);
				}
				if(count == file_size){close(fd);close(cl);continue;}
				
				// receive the rest of file data
				memset(file_buf, 0, BUFFER_SIZE);
				while((recv_result = recv(cl, file_buf, sizeof(file_buf) - 1, 0)) != 0){
					write_result = write(fd, file_buf, recv_result);
					count += write_result;
					if(count == file_size){break;}
					memset(file_buf, 0, sizeof(file_buf));
				}
			}
		}
		
		//GET request
		else if (strcmp(action, "GET") == 0){
			// check httpfile validation
			if((fd = open(httpname, O_RDONLY)) == -1){
				send(cl, "HTTP/1.1 404 Not Found\r\n\r\n", 26, 0);
			}else{
				memset(file_buf, 0, sizeof(file_buf));
				if((read_result = read(fd, file_buf, sizeof(file_buf))) == -1){
					if(errno == 21){
						send(cl, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0);
					}else if(errno == 13){
						send(cl, "HTTP/1.1 403 Forbidden\r\n\r\n", 26, 0);
					}
				}else{
					// send response: status code
					send(cl, "HTTP/1.1 200 OK\r\n", 17, 0);
					
					// send response: Content-Length
					memset(buf, 0, sizeof(buf));
					struct stat st;
					stat(httpname, &st);
					file_size = st.st_size;
					strcat(buf, "Content-Length: ");
					char tem[sizeof(int32_t)*8+1];
					sprintf(tem,"%d",file_size);
					strcat(buf, tem);
					strcat(buf, "\r\n\r\n");
					send(cl, buf, strlen(buf), 0);
					
					// send data
					send(cl, file_buf, read_result, 0);
					do{
						memset(file_buf, 0, sizeof(file_buf));
						if((read_result = read(fd, file_buf, sizeof(file_buf))) != 0){
							send(cl, file_buf, read_result, 0);
						}
					}while(read_result != 0);
				}
			}
		}
		
		// other -> bad request
		else{
			send(cl, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0);
		}
		close(fd);
		close(cl);
	}
	return 0;
}