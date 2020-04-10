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

#define BUFFER_SIZE 4096 /* 4 KB maximum buffer size */

const int NUM_THREADS = 30;

pthread_mutex_t lock;
int x = 0;

struct thread_info {
	int id;
};

void *start_fn(void *arg)
{
	struct thread_info *ti = (thread_info*)arg; 
	printf("Hello from spawned thread! : %d\n", ti->id);
	pthread_mutex_lock(&lock);
	for(int i = 0; i < 1000; i++) {
		x++;
	}
	pthread_mutex_unlock(&lock);
	//for(;;);
	return (NULL);
}

int main()
{
	pthread_mutex_init(&lock, NULL);

	pthread_t thread[NUM_THREADS];
	struct thread_info tinfo[NUM_THREADS];

	for(unsigned i = 0; i < NUM_THREADS; i++) {
		tinfo[i].id = i;
		pthread_create(&thread[i], NULL, start_fn, &tinfo[i]);
	}
	// ...
}