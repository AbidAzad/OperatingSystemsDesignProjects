// File:	worker_t.h

// List all group member's name:
// username of iLab:
// iLab Server:

#ifndef WORKER_T_H
#define WORKER_T_H

#define _GNU_SOURCE

/* To use Linux pthread Library in Benchmark, you have to comment the USE_WORKERS macro */
#define USE_WORKERS 1

/* include lib header files that you need here: */
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <signal.h>
#include <stdatomic.h>

#define STACK_SIZE 8192 //SIGSTKSZ = 8192

typedef uint worker_t;
#define READY 0
#define SCHEDULED 1
#define BLOCKED_MUTEX 2
#define BLOCKED_JOIN 3
#define FINISHED 4

typedef struct TCB {
	/* add important states in a thread control block */
	// thread Id
	uint TID;
	// thread status
	int status;
	// thread context
	ucontext_t context;
	// thread stack
	char stack[STACK_SIZE];
	// thread priority
    uint joiningThread;
	// And more ...
    void* retVal;
	// YOUR CODE HERE
} tcb; 

typedef struct Node{
	tcb* data;
    struct Node* next;
}Node;

typedef struct Queue{
	Node* head;
	Node* tail;
    int _size;
}Queue;


/* mutex struct definition */
typedef struct worker_mutex_t {
	/* add something here */

	// YOUR CODE HERE
	atomic_flag flag;
	Queue threadQueue;
	uint owner;

} worker_mutex_t;

/* define your data structures here: */
// Feel free to add your own auxiliary data structures (linked list or queue etc...)

// YOUR CODE HERE





typedef struct HNode {
    tcb* value;
    int key;
}HNode;

typedef struct HashMap {
    struct HNode **arr;
    int capacity;
    int msize;
    struct HNode *dummy;
    int initialized;
}HashMap;



/* Function Declarations: */

/* create a new thread */
int worker_create(worker_t * thread, pthread_attr_t * attr, void
    *(*function)(void*), void * arg);

/* give CPU pocession to other user level worker threads voluntarily */
int worker_yield();

/* terminate a thread */
void worker_exit(void *value_ptr);

/* wait for thread termination */
int worker_join(worker_t thread, void **value_ptr);

/* initial the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t
    *mutexattr);

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex);

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex);

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex);


/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void);

#ifdef USE_WORKERS
#define pthread_t worker_t
#define pthread_mutex_t worker_mutex_t
#define pthread_create worker_create
#define pthread_exit worker_exit
#define pthread_join worker_join
#define pthread_mutex_init worker_mutex_init
#define pthread_mutex_lock worker_mutex_lock
#define pthread_mutex_unlock worker_mutex_unlock
#define pthread_mutex_destroy worker_mutex_destroy
#endif

#endif
