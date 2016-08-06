#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "pipe.h"

int init_queue(struct queue* q, int n)
{
	int default_sz = sizeof(q->_elems) / sizeof(q->_elems[0]);
	
	q->elems = n <= default_sz ? &q->_elems[0] : 
		malloc(n * sizeof(q->elems[0]));
	if (q->elems == NULL)
		return -1;

	q->head = 0;
	q->n = 0;
	q->max = n;
	return 0;
}

void close_queue(struct queue* q)
{
	if (q->elems != &q->_elems[0]) { 
		free(q->elems);
	}
}

int enqueue(struct queue* q, void* elem)
{
	int idx = (q->head + q->n) % q->max;

	if (q->n >= q->max)
		return -1;

	q->elems[idx] = elem;
	q->n++;
	return 0;
}

int dequeue(struct queue* q, void** elem)
{
	if (q->n <= 0)
		return -1;
	
	*elem = q->elems[q->head];
	q->head = (q->head + 1) % q->max;
	q->n--;
	return 0;
}

void iterate_queue(struct queue* q, void (*p)(void* elem))
{
	int n = q->n;
	int idx = q->head;
	
	while (n-- > 0) {
		p(q->elems[idx]);
		idx = (idx + 1) % q->max;
	}
}

// -----

struct pipe_elem {
	void* buf;
	int seq;
	int ref_cnt;
};

int init_pipe(struct pipe* p, int n_dst, int q_depth, int buf_sz)
{
	int free_bufs = n_dst * q_depth;
	struct pipe_elem* msg_all = 
		malloc(free_bufs * (sizeof(msg_all[0]) + buf_sz));
	void* buf_ptr;
	int def_dst_n, i;

	// allocate bufs
	if (!msg_all)
		return -1;
	p->priv = (void*)msg_all;

	// initialize src
	if (init_queue(&p->src, free_bufs))
		goto free_buf;
	
	// initialize dst
	def_dst_n = sizeof(p->_dst) / sizeof(p->_dst[0]);
	p->dst = n_dst < def_dst_n ? &p->_dst[0] :
		malloc(n_dst * sizeof(p->dst[0]));
	if (!p->dst)
		goto free_src;
	for (i = 0; i < n_dst; i++) {
		if (init_queue(&p->dst[i], q_depth))
			goto free_dst;
	}

	// init spinlock
	if (pthread_spin_init(&p->lock, PTHREAD_PROCESS_SHARED)) 
		goto free_dst;

	// push buffers onto src
	buf_ptr = (void*)&msg_all[free_bufs];
	for (i = 0; i < free_bufs; i++) {
		msg_all[i].buf = buf_ptr;
		msg_all[i].seq = 0;
		msg_all[i].ref_cnt = 0;
		buf_ptr += buf_sz;

		assert(!enqueue(&p->src, &msg_all[i]));
	}

	p->n_dst = n_dst;
	return 0;

free_dst : 
	for (i--; i >= 0; i--)
		close_queue(&p->dst[i]);
free_src : 
	close_queue(&p->src);
free_buf : 
	free(msg_all);
destroy_lock : 
	pthread_spin_destroy(&p->lock);
	
	return -1;
}

void close_pipe(struct pipe* p)
{
	int i;
	
	for (i = 0; i < p->n_dst; i++)
		close_queue(&p->dst[i]);
	if (p->dst != &p->_dst[0])
		free(p->dst);
	close_queue(&p->src);
	free(p->priv);
	pthread_spin_destroy(&p->lock);
	
}

void* get_buf(struct pipe* p, void** pbuf)
{
	struct pipe_elem* elem = NULL;

	// lock
	pthread_spin_lock(&p->lock);
	if (!dequeue(&p->src, (void**)&elem))
		*pbuf = elem->buf;

	//unlock
	pthread_spin_unlock(&p->lock);

	return elem;
}

int push_buf(struct pipe* p, void* handle, int seq)
{
	struct pipe_elem* elem = handle;
	int i, ret;

	assert(handle);

	// lock
	pthread_spin_lock(&p->lock);

	elem->seq = seq;
	elem->ref_cnt = 0;
	for (i = 0; i < p->n_dst; i++) {
		if (!enqueue(&p->dst[i], elem))
			elem->ref_cnt++;
	}

	if (!elem->ref_cnt) {
		// add back to free
		assert(!enqueue(&p->src, elem));
	}

	ret = elem->ref_cnt;

	// unlock
	pthread_spin_unlock(&p->lock);

	return ret;
}

void* pull_buf(struct pipe* p, int id, const void** buf, int* seq)
{
	struct pipe_elem* elem = NULL;

	// lock
	pthread_spin_lock(&p->lock);

	if (id < 0 || id >= p->n_dst)
		goto unlock;

	if (!dequeue(&p->dst[id], (void**)&elem)) {
		*buf = elem->buf;
		*seq = elem->seq;
	}

	// unlock
unlock : 
	pthread_spin_unlock(&p->lock);

	return elem;
}

void put_buf(struct pipe* p, void* handle)
{
	struct pipe_elem* elem = handle;

	// lock
	pthread_spin_lock(&p->lock);

	elem->ref_cnt--;
	if (elem->ref_cnt <= 0) {
		assert(elem->ref_cnt >= 0);
		assert(!enqueue(&p->src, elem));
	}

	// unlock
	pthread_spin_unlock(&p->lock);
}

void print_pipe_elem(void* p)
{
	struct pipe_elem* elem = p;

	printf(" %p : %p %d %d\n", elem, elem->buf, 
		elem->seq, elem->ref_cnt);
}

void print_pipe(struct pipe* p)
{
	int i;

	printf("print src\n");
	iterate_queue(&p->src, print_pipe_elem);
	
	for (i = 0; i < p->n_dst; i++) {
		printf("print dst %d\n", i);
		iterate_queue(&p->dst[i], print_pipe_elem);
	}
}

#ifdef PIPE_TEST
int main(int argc, char* argv[])
{
	struct pipe p;
	void* h;
	void* h0, *h1, *h2;
	void* b;
	const void* b0, *b1, *b2;
	int s0, s1, s2;

	// init
	printf("%d\n", init_pipe(&p, 3, 2, 0x1));

#if 0
	//get and push
	h0 = get_buf(&p, &b);
	printf("%p %p\n", h0, b);
	printf("%d\n", push_buf(&p, h0, 1));

	print_pipe(&p);

	h0 = pull_buf(&p, 0, &b0, &s0);
	h1 = pull_buf(&p, 1, &b1, &s1);
	h2 = pull_buf(&p, 2, &b2, &s2);
	put_buf(&p, h0);
	put_buf(&p, h1);
	put_buf(&p, h2);

	print_pipe(&p);

	h0 = get_buf(&p, &b);
	printf("%p %p\n", h0, b);
	printf("%d\n", push_buf(&p, h0, 1));
#endif

	// src push 0 1 2
	h = get_buf(&p, &b); push_buf(&p, h, 1);
	// src push 0 1 2
	h = get_buf(&p, &b); push_buf(&p, h, 2);

	// dst pull 1 2
	h2 = pull_buf(&p, 2, &b2, &s2); put_buf(&p, h2);
	h1 = pull_buf(&p, 1, &b1, &s1); put_buf(&p, h1);
	//print_pipe(&p); exit(0);

	// src push 1 2 (0 is full)
	h = get_buf(&p, &b); push_buf(&p, h, 3);
	//print_pipe(&p); exit(0);

	// dst pull 1 2
	h2 = pull_buf(&p, 2, &b2, &s2); put_buf(&p, h2);
	h1 = pull_buf(&p, 1, &b1, &s1); put_buf(&p, h1);
	//print_pipe(&p); exit(0);

	// dst pull 1 2
	h2 = pull_buf(&p, 2, &b2, &s2); put_buf(&p, h2);
	h1 = pull_buf(&p, 1, &b1, &s1); put_buf(&p, h1);
	//print_pipe(&p); exit(0);

	// dst pull 0
	h0 = pull_buf(&p, 0, &b0, &s0); put_buf(&p, h0);

	// dst pull 0
	h0 = pull_buf(&p, 0, &b0, &s0); put_buf(&p, h0);
	//print_pipe(&p); exit(0);

	// src push 0 1 2
	h = get_buf(&p, &b); push_buf(&p, h, 4);
	// src push 0 1 2
	h = get_buf(&p, &b); push_buf(&p, h, 5);
	// src push 0 1 2
	h = get_buf(&p, &b); push_buf(&p, h, 6);
	// src push 0 1 2
	h = get_buf(&p, &b); push_buf(&p, h, 7);
	//print_pipe(&p); exit(0);

	// dst pull 0
	h0 = pull_buf(&p, 0, &b0, &s0); put_buf(&p, h0);
	// dst pull 0
	h0 = pull_buf(&p, 0, &b0, &s0); put_buf(&p, h0);
	h0 = pull_buf(&p, 0, &b0, &s0); 
	printf("%p\n", h0);

	print_pipe(&p); exit(0);
		//should be src(0), dst0(0), dst1(2), dst2(2)

#if 0
	struct queue q;
	void* p;
	init_queue(&q, 5);
	printf("%d\n", enqueue(&q, (void*)1));
	printf("%d\n", enqueue(&q, (void*)2));
	printf("%d\n", enqueue(&q, (void*)3));
	printf("%d\n", enqueue(&q, (void*)4));
	printf("%d\n", enqueue(&q, (void*)5));
	printf("%d\n", enqueue(&q, (void*)6));
	printf("\n");
	printf("%d ", dequeue(&q, &p)); printf("%p\n", p);
	printf("%d ", dequeue(&q, &p)); printf("%p\n", p);
	printf("%d ", dequeue(&q, &p)); printf("%p\n", p);
	printf("%d ", dequeue(&q, &p)); printf("%p\n", p);
	printf("%d ", dequeue(&q, &p)); printf("%p\n", p);
	printf("%d ", dequeue(&q, &p)); printf("%p\n", p);

	printf("\n\n");
	init_queue(&q, 7);
	printf("%d\n", enqueue(&q, (void*)1));
	printf("%d\n", enqueue(&q, (void*)2));
	printf("%d\n", enqueue(&q, (void*)3));
	printf("%d\n", enqueue(&q, (void*)4));
	printf("%d\n", enqueue(&q, (void*)5));
	printf("%d\n", enqueue(&q, (void*)6));
	printf("%d\n", enqueue(&q, (void*)7));
	printf("%d\n", enqueue(&q, (void*)8));
	printf("\n");
	printf("%d ", dequeue(&q, &p)); printf("%p\n", p);
	printf("%d ", dequeue(&q, &p)); printf("%p\n", p);
	printf("%d ", dequeue(&q, &p)); printf("%p\n", p);
	printf("%d ", dequeue(&q, &p)); printf("%p\n", p);
	printf("%d ", dequeue(&q, &p)); printf("%p\n", p);
	printf("%d ", dequeue(&q, &p)); printf("%p\n", p);
	printf("%d ", dequeue(&q, &p)); printf("%p\n", p);
	printf("%d ", dequeue(&q, &p)); printf("%p\n", p);
#endif

	return 0;
}
#endif
