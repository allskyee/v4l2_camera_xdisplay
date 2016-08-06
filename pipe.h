#ifndef __PIPE_H__
#define __PIPE_H__

#include <pthread.h>

struct queue { 
	void* _elems[5];
	void** elems;
	int head;
	int n;
	int max;
};

int  init_queue(struct queue* q, int n);
void close_queue(struct queue* q);
int  enqueue(struct queue* q, void* elem);
int  dequeue(struct queue* q, void** elem);
void iterate_queue(struct queue* q, void (*p)(void* elem));

// --------

struct pipe {
	struct queue src;
	struct queue _dst[3];
	struct queue* dst;
	int n_dst;

	pthread_spinlock_t lock;

	void* priv; //a hidden datastructure
};

void* get_buf(struct pipe* p, void** pbuf);
	// returns handle to buffer
int push_buf(struct pipe* p, void* handle, int seq);
	// returns number of messages successfully delivered
	// if 0, then buffer is automatically recycled
void* pull_buf(struct pipe* p, int id, const void** buf, int* seq);
	// returns handle of buffer (must use for return)
void put_buf(struct pipe* p, void* handle);

int  init_pipe(struct pipe* p, int n_dst, int q_depth, int buf_sz);
void close_pipe(struct pipe* p);

void print_pipe(struct pipe* p);

#endif
