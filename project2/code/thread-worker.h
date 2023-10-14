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
	// And more ...

	// YOUR CODE HERE
} tcb; 

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

///////////////MAKING A QUEUE DATA STRUCTURE FOR TCB//////////////////////////////////////
typedef struct Node{
	tcb thread;
	struct Node* next;
}Node;

typedef struct Queue{
	Node* front;
	Node* rear;
}Queue;

void initializeQueue(Queue* queue) {
    queue->front = queue->rear = NULL;
}

int isQueueInitialized(const Queue* queue) {
    return (queue->front == NULL && queue->rear == NULL);
}

int isQueueEmpty(Queue* queue) {
    return queue->front == NULL;
}

void enqueue(Queue* queue, tcb thread) {
    Node* newNode = (Node*)malloc(sizeof(Node));
    if (newNode == NULL) {
        fprintf(stderr, "Memory allocation error\n");
        exit(EXIT_FAILURE);
    }

    newNode->thread = thread;
    newNode->next = NULL;

    if (isQueueEmpty(queue)) {
        queue->front = queue->rear = newNode;
    } else {
        queue->rear->next = newNode;
        queue->rear = newNode;
    }
}

tcb dequeue(Queue* queue) {
    if (isQueueEmpty(queue)) {
        fprintf(stderr, "Queue is empty\n");
        exit(EXIT_FAILURE);
    }

    Node* temp = queue->front;
    tcb thread = temp->thread;

    queue->front = temp->next;
    free(temp);

    if (queue->front == NULL) {
        queue->rear = NULL; 
    }

    return thread;
}

tcb peek(Queue* queue) {
    if (isQueueEmpty(queue)) {
        fprintf(stderr, "Queue is empty\n");
        exit(EXIT_FAILURE);
    }

    return queue->front->thread;
}

void printQueue(Queue* queue) {
    Node* current = queue->front;
    while (current != NULL) {
        // Print information about the tcb (e.g., TID, status)
        printf("TID: %u, Status: %d\n", current->thread.TID, current->thread.status);
        current = current->next;
    }
}
///////////////////////////////////////////////////

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
