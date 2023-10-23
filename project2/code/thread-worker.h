// File:	worker_t.h

// List all group member's name: Abid Azad, Ghautham Sambabu 
// username of iLab: rlab
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
#include <limits.h>

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
	int elapsed;
	struct timeval start_time;
	struct timeval end_time;
} tcb; 

typedef struct Node{
	tcb* data;
    struct Node* next;
}Node;

typedef struct Queue{
	Node* front;
	Node* back;
    int queueSize;
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

////////////////QUEUE DATA STRUCTURE


int isLastQueue(int queueNum);
void initializeQueue(Queue* startingQueue);
int isQueueEmpty(Queue* queue);
int queueSize(Queue* q);
tcb* queueGet(Queue* queue, int position);
void enqueue(Queue* queue, tcb* thread);
tcb* dequeue(Queue* queue);
tcb* peek(Queue* queue);
tcb* getLast(Queue* queue);
void queueClear(Queue* queue);
////////////////////////////////////////////////

////////////////////HASHMAP DATA STRUCTURE/////////////////
typedef struct HNode {
    tcb* value;
    int key;
}HNode;

typedef struct HashMap {
    struct HNode **arr;
    int hashMapCapacity;
    int hashMapSize;
    struct HNode *dummy;
    int initialized;
}HashMap;

int hash(struct HashMap *map, int key);
void initHashMap(struct HashMap *map);
void put(struct HashMap *map, int key, tcb* value);
tcb* removeFromHashMap(HashMap *map, int key);
tcb* getFromHashMap(struct HashMap *map, int key);
int hashMapSize(struct HashMap *map);
int hashMapEmpty(struct HashMap *map);
////////////////////////////////////////////////////////////


//////////////////Helper Functions/////////////////////////////
void worker_start(tcb *currTCB, void (*function)(void *), void *arg);
static void schedule();
static void sched_mlfq();
static void sched_psjf();
void createContext(ucontext_t* threadContext);
void createSchedulerContext();
void createMainContext();
void setupTimer();
int isThreadInactive(int queueNum);
////////////////////////////////////////////////////////////////



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
