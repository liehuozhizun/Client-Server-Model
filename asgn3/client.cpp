/*
 * Hang Yuan
 * 1564348, hyuan3
 *
 * ASGN 3: Key-value store HTTP server
 * This program implements the multi-threaded HTTP server with Key-value store
 * functionality.
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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define BUFFER_SIZE 4096 /* 4 KB maximum buffer size */

int32_t main(int32_t argc, char *argv[]) {
	char file_buf[BUFFER_SIZE], buf[BUFFER_SIZE], address[33];
	int32_t port_number;
	char *ptr, *ptr_tem, *ptr_content_length;

	if (argc == 1) {
		fprintf(stderr, "SET UP FAILED: no valid address and port number\n");
		exit(EXIT_FAILURE);
	}

	// Obtain the address and port number
	if ((ptr = strchr(argv[1], ':')) == NULL) {
		fprintf(stderr, "SET UP FAILED: not valid address and port number\n");
		exit(EXIT_FAILURE);
	}
	strncpy(address, ptr + 1, (strlen(argv[1]) - (ptr - argv[1] + 1)));

	if ((port_number = atoi(address)) == 0) {
		port_number = 80;
	}
	strncpy(address, argv[1], (ptr - argv[1]));

	// set up connection
	struct hostent *hent = gethostbyname(address);
	if (hent == NULL) {
		fprintf(stderr, "%s\n", strerror(h_errno));
		exit(EXIT_FAILURE);
	}

	for (int32_t i = 2; i < argc; i++) {
		// clean buffer
		memset(buf, 0, BUFFER_SIZE);
		memset(file_buf, 0, BUFFER_SIZE);

		// obtain action, filename, and httpname
		char action[42], filename[100], httpname[42];
		int32_t fd = 0, cl;
		size_t file_size = -1;
		ssize_t read_result, recv_result, write_result;
		memset(action, 0, sizeof(action));
		memset(filename, 0, sizeof(filename));
		memset(httpname, 0, sizeof(httpname));
		if ((ptr = strstr(argv[i], ":")) == NULL) {
			fprintf(stderr, "HTTP/1.1 400 Bad Request\n");
			continue;
		}
		strncpy(action, argv[i], ptr - argv[i]);
		if ((strcmp(action, "r") != 0) && (strcmp(action, "s") != 0)) {
			fprintf(stderr, "HTTP/1.1 400 Bad Request\n");
			continue;
		}
		strncpy(buf, ptr + 1, strlen(ptr + 1));
		if ((ptr = strstr(buf, ":")) == NULL) {
			fprintf(stderr, "HTTP/1.1 400 Bad Request\n");
			continue;
		}
		if ((int32_t)strlen(argv[i]) == (ptr - argv[i])) {
			fprintf(stderr, "HTTP/1.1 400 Bad Request\n");
			continue;
		}
		if (strcmp(action, "r") == 0) {
			strncpy(httpname, buf, ptr - buf);
			strncpy(filename, ptr + 1, strlen(buf) - (ptr - buf) - 1);
		} else {
			strncpy(filename, buf, ptr - buf);
			strncpy(httpname, ptr + 1, strlen(buf) - (ptr - buf) - 1);
		}
		memset(buf, 0, BUFFER_SIZE);

		// check httpname should be 40 characters
		if (strlen(httpname) != 40) {
			fprintf(stderr, "HTTP/1.1 400 Bad Request\n");
			continue;
		}

		// set up sock
		struct sockaddr_in addr;
		memcpy(&addr.sin_addr.s_addr, hent->h_addr, hent->h_length);
		addr.sin_port = htons(port_number);
		addr.sin_family = AF_INET;
		int32_t sock = socket(AF_INET, SOCK_STREAM, 0);

		// connect() == -1 means failed connection to server
		if ((cl = connect(sock, (struct sockaddr *)&addr, sizeof(addr))) == -1) {
			fprintf(stderr, "%s\n", strerror(errno));
			continue;
		}

		// s -> PUT request
		if (argv[i][0] == 's') {
			// check validation of local file
			if ((fd = open(filename, O_RDONLY)) == -1) {
				fprintf(stderr, "%s\n", strerror(errno));
			} else {
				if ((read_result = read(fd, file_buf, sizeof(file_buf))) == -1) {
					fprintf(stderr, "%s\n", strerror(errno));
				} else {
					// send request header
					strcat(buf, "PUT ");
					strcat(buf, httpname);
					strcat(buf, " HTTP/1.1\r\n");
					struct stat st;
					stat(filename, &st);
					strcat(buf, "Content-Length: ");
					char tem[sizeof(size_t) * 8 + 1];
					sprintf(tem, "%zu",	st.st_size);
					strcat(buf, tem);
					strcat(buf, "\r\n\r\n");
					if (send(sock, buf, strlen(buf), 0) == -1) {
						fprintf(stderr, "%s\n", strerror(errno));
					} else {
						// send data
						memset(buf, 0, BUFFER_SIZE);
						file_size = 0;
						send(sock, file_buf, read_result, 0);
						file_size += read_result;

						// send the rest of file data
						do {
							memset(file_buf, 0, sizeof(file_buf));
							read_result = read(fd, file_buf, sizeof(file_buf));
							send(sock, file_buf, read_result, 0);
							file_size += read_result;
						} while (read_result != 0);

						// receive response
						recv_result = recv(sock, buf, sizeof(buf) - 1, 0);
					}
				}
			}
		}

		// r -> GET request
		else if (argv[i][0] == 'r') {
			// send request header
			strcat(buf, "GET ");
			strcat(buf, httpname);
			strcat(buf, " HTTP/1.1\r\n\r\n");
			if (send(sock, buf, strlen(buf), 0) == -1) {
				fprintf(stderr, "%s\n", strerror(errno));
			} else {
				// receive response: status code or may be more
				memset(buf, 0, sizeof(buf));
				if ((recv_result = recv(sock, buf, sizeof(buf) - 1, MSG_WAITALL)) == -1) {
					fprintf(stderr, "%s\n", strerror(errno));
				} else {
					// detect status code response
					ptr = strstr(buf, "\r\n");
					strncpy(file_buf, buf,
									ptr - buf); // status code stored in file temporarily
					if ((strstr(file_buf, "200 OK") == NULL) &&
							(strstr(file_buf, "201 Created") == NULL)) {
						fprintf(stderr, "%s\n", file_buf);
						close(sock);
						continue;
					}
					memset(file_buf, 0, sizeof(file_buf));

					// check validation of local file_buf
					if ((fd = open(filename, O_WRONLY | O_TRUNC)) == -1) {
						if ((fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC)) == -1) {
							fprintf(stderr, "%s\n", strerror(errno));
							close(fd);
							close(sock);
							continue;
						}
					}
					// detect Content-Length
					ptr_content_length = strstr(buf, "Content-Length: ");
					ptr = strstr(ptr_content_length, " ");
					ptr_tem = strstr(ptr_content_length, "\r\n");
					strncpy(file_buf, ptr + 1, ptr_tem - ptr - 1);
					file_size = atoi(file_buf);
					ptr_tem = strstr(buf, "\r\n\r\n");
					if (recv_result == (ptr_tem - buf + 4)) { // NO data received
						close(fd);
						close(sock);
						continue;
					} else { // data received
						memset(file_buf, 0, sizeof(file_buf));
						strncpy(file_buf, ptr_tem + 4, strlen(ptr_tem + 4));
					}

					// receive the rest of data
					do {
						if ((write_result = write(fd, file_buf, strlen(file_buf))) == -1) {
							fprintf(stderr, "%s\n", strerror(errno));
							break;
						}
						file_size -= write_result;
						if (file_size == 0) { // use Content-Length to check end of file
							break;
						}
						memset(file_buf, 0, sizeof(file_buf));
						recv_result = recv(sock, file_buf, sizeof(file_buf) - 1, 0);
					} while (recv_result != 0 && write_result != 0);
				}
			}
		}
		close(fd);
		close(sock);
	}
	return 0;
}
