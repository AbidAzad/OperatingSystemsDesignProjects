// File:	thread-worker.c

// List all group member's name:
// username of iLab:
// iLab Server:

#include "thread-worker.h"

//Global counter for total context switches and 
//average turn around and response time
long tot_cntx_switches=0;
double avg_turn_time=0;
double avg_resp_time=0;


// INITAILIZE ALL YOUR OTHER VARIABLES HERE
// YOUR CODE HERE
#define SCHEDULER_THREAD 0
#define MAIN_THREAD 1
#define QUEUE_NUM 4
#define TIMESLICE 5
int threadCounter = 2; // Global var to assign thread ids
Queue threadQueue[QUEUE_NUM];
int started = 1;

HashMap *map = NULL;
int isSchedCreated = 0;
tcb* get_current_tcb();
tcb* get_scheduler_tcb();
int isYielding = 0;
uint currentThreadTNum = MAIN_THREAD;
int currentThreadQNum=0;
int isDebugging = 0;
enum sched_options {_PSJF, _MLFQ};
int SCHED_TYPE = _MLFQ;
static void schedule();
static void sched_mlfq();
static void sched_psjf();
void createContext(ucontext_t* threadContext);
void createSchedulerContext();
void createMainContext();
void setupTimer();
int isThreadInactive(int queueNum);
int isLastQueue(int queueNum);
void initializeQueue(Queue* q);
int isQueueEmpty(Queue* queue);
int size(Queue* q);
tcb* get(Queue* q, int i);
void enqueue(Queue* q, tcb* x);
tcb* dequeue(Queue* q);
tcb* peek(Queue* q);
tcb* getTail(Queue* q);
void clear(Queue* q);
int hash(struct HashMap *map, int key);
void initHashMap(struct HashMap *map);
void put(struct HashMap *map, int key, tcb* value);
tcb* removeFromHashMap(HashMap *map, int key);
tcb* getFromHashMap(struct HashMap *map, int key);
int hashMapSize(struct HashMap *map);
int isEmpty(struct HashMap *map);
void worker_start(tcb *currTCB, void (*function)(void *), void *arg);
int qSize(Queue* q);

/* create a new thread */
int worker_create(worker_t * thread, pthread_attr_t * attr, 
                      void *(*function)(void*), void * arg) {
		
		if(started){
            for(int i = 0; i<QUEUE_NUM;i++){
                initializeQueue(&threadQueue[i]);
            }
			
			started=0;
		}

		if (map == NULL){
			map = (HashMap *)malloc(sizeof(HashMap));
			initHashMap(map);
		}
       // - create Thread Control Block (TCB)
	   tcb* newThread = (tcb*) malloc(sizeof(tcb));
       // - create and initialize the context of this worker thread
	   newThread->TID = threadCounter;
	   newThread->status = READY;
	   newThread->joiningThread = 0;
	   
       // - allocate space of stack for this thread to run
	   createContext(&newThread->context);
       getcontext(&newThread->context);
	   
       // after everything is set, push this thread into run queue and 
       // - make it ready for the execution.
	   makecontext(&newThread->context, (void (*)()) &worker_start, 3, newThread, function, arg);
       put(map, threadCounter++, newThread);

	   if (!isSchedCreated) {
       	createSchedulerContext();
        createMainContext();

        enqueue(&threadQueue[0], getFromHashMap(map, MAIN_THREAD));
        enqueue(&threadQueue[0], newThread);

        isSchedCreated = 1;

	    

        setupTimer();
    } else {
       enqueue(&threadQueue[0], newThread);
    }
       // YOUR CODE HERE
	
    return 0;
}

void worker_start(tcb *currTCB, void (*function)(void *), void *arg) {
    function(arg);
    currTCB->status = FINISHED;
    free(currTCB->context.uc_stack.ss_sp);

    setcontext(&get_scheduler_tcb()->context);
}

/* give CPU possession to other user-level worker threads voluntarily */
int worker_yield() {
	
	// - change worker thread's state from Running to Ready
	// - save context of this thread to its thread control block
	// - switch from thread context to scheduler context
	tcb* currTCB = get_current_tcb();
    currTCB->status = READY;

    isYielding = 1;

    swapcontext(&currTCB->context, &get_scheduler_tcb()->context);
	// YOUR CODE HERE
	
	return 0;
};

/* terminate a thread */
void worker_exit(void *value_ptr) {
	// - de-allocate any dynamic memory created when starting this thread
	tcb* currTCB = get_current_tcb();
    currTCB->retVal = value_ptr;
    free(currTCB->context.uc_stack.ss_sp);

    if (currTCB->joiningThread != 0) {
        tcb* joinedTCB = getFromHashMap(map, currTCB->joiningThread);
        joinedTCB->status = READY;
        enqueue(&threadQueue[0],joinedTCB);
    }

    currTCB->status = FINISHED;
    setcontext(&get_scheduler_tcb()->context);
	// YOUR CODE HERE
};


/* Wait for thread termination */
int worker_join(worker_t thread, void **value_ptr) {
	
	// - wait for a specific thread to terminate
	// - de-allocate any dynamic memory created by the joining thread
	tcb* currTCB = get_current_tcb();
    tcb* joinedTCB = getFromHashMap(map, thread);

    if (joinedTCB->status != FINISHED) {
        joinedTCB->joiningThread = currTCB->TID;
        currTCB->status = BLOCKED_JOIN;

        swapcontext(&currTCB->context, &get_scheduler_tcb()->context);
    }

    if(value_ptr != NULL) {
        *value_ptr = joinedTCB->retVal;
    }

	// YOUR CODE HERE
	return 0;
};

/* initialize the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, 
                          const pthread_mutexattr_t *mutexattr) {
	//- initialize data structures for this mutex

	// YOUR CODE HERE
	return 0;
};

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex) {

        // - use the built-in test-and-set atomic function to test the mutex
        // - if the mutex is acquired successfully, enter the critical section
        // - if acquiring mutex fails, push current thread into block list and
        // context switch to the scheduler thread

        // YOUR CODE HERE
		while (atomic_flag_test_and_set(&mutex->flag)) {
			tcb* currTCB = get_current_tcb();
			enqueue(&(mutex->threadQueue), currTCB);
			currTCB->status = BLOCKED_MUTEX;
			swapcontext(&currTCB->context, &get_scheduler_tcb()->context);
		}

		mutex->owner = get_current_tcb()->TID;
        return 0;
};

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex) {
	// - release mutex and make it available again. 
	// - put threads in block list to run queue 
	// so that they could compete for mutex later.

	// YOUR CODE HERE
 if(get_current_tcb()->TID != mutex->owner) {
        printf("Unauthorized Thread Unlocking.");
        exit(1);
    }

    if(isQueueEmpty(&(mutex->threadQueue))) {
        for (int i = 0; i < qSize(&(mutex->threadQueue)); i++) {
            tcb* x = get(&(mutex->threadQueue), i);
            getFromHashMap(map, x->TID)->status = READY;
            enqueue(&threadQueue[0], getFromHashMap(map, x->TID));
        }

        clear(&(mutex->threadQueue));
    }
    atomic_flag_clear_explicit(&mutex->flag, memory_order_seq_cst);




	return 0;
};


/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex) {
	// - de-allocate dynamic memory created in worker_mutex_init
    clear(&(mutex->threadQueue));
	return 0;
};

/* scheduler */
static void schedule() {
	// - every time a timer interrupt occurs, your worker thread library 
	// should be contexted switched from a thread context to this 
	// schedule() function

	// - invoke scheduling algorithms according to the policy (PSJF or MLFQ)

	// if (sched == PSJF)
	//		sched_psjf();
	// else if (sched == MLFQ)
	// 		sched_mlfq();

	// YOUR CODE HERE
    currentThreadTNum= SCHEDULER_THREAD;
    sched_mlfq();

}

/* Pre-emptive Shortest Job First (POLICY_PSJF) scheduling algorithm */
static void sched_psjf() {
	// - your own implementation of PSJF
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE
}


/* Preemptive MLFQ scheduling algorithm */
static void sched_mlfq() {
	// - your own implementation of MLFQ
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE
	int qNum = currentThreadQNum;

    if (isThreadInactive(qNum)) {
        dequeue(&threadQueue[qNum]);
    } else if(isYielding || isLastQueue(qNum)) {
        enqueue(&threadQueue[qNum], dequeue(&threadQueue[qNum]));
    } else {
        enqueue(&threadQueue[qNum+1], dequeue(&threadQueue[qNum]));
    }
    isYielding = 0;

    int queueNum = 0;
    while (queueNum < QUEUE_NUM) {
		if (isQueueEmpty(&threadQueue[queueNum])){++queueNum; continue;}

    tcb* currTCB = peek(&threadQueue[queueNum]);
	currentThreadTNum = currTCB->TID;
	currentThreadQNum = queueNum;
    setcontext(&currTCB->context);
        ++queueNum;
    }
}

//DO NOT MODIFY THIS FUNCTION
/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void) {

       fprintf(stderr, "Total context switches %ld \n", tot_cntx_switches);
       fprintf(stderr, "Average turnaround time %lf \n", avg_turn_time);
       fprintf(stderr, "Average response time  %lf \n", avg_resp_time);
}


// Feel free to add any other functions you need

// YOUR CODE HERE
void timer_handler(int signum) {

    // setupTimer expired, schedule next thread
    if (currentThreadTNum != SCHEDULER_THREAD) {
        swapcontext(&get_current_tcb()->context, &get_scheduler_tcb()->context);
    }
}
void setupTimer() {
    struct itimerval it_val;	/* for setting itimer */

    if (signal(SIGALRM, timer_handler) == SIG_ERR) {
        perror("Unable to catch SIGALRM");
        exit(1);
    }
    it_val.it_value.tv_sec =     TIMESLICE/1000;
    it_val.it_value.tv_usec =    (TIMESLICE*1000) % 1000000;
    it_val.it_interval = it_val.it_value;
    if (setitimer(ITIMER_REAL, &it_val, NULL) == -1) {
        perror("error calling setitimer()");
        exit(1);
    }

    int isTimerFiredOnce = 0;
    while (!isTimerFiredOnce) {
        pause();
        isTimerFiredOnce = 1;
    }
}
void stopTimer() {
    struct itimerval it_val;
    it_val.it_interval.tv_usec = 0;
    it_val.it_interval.tv_sec = 0;
    it_val.it_value.tv_usec = 0;
    it_val.it_value.tv_sec = 0;
    setitimer(ITIMER_REAL, &it_val, NULL);
}

void startTimer() {
    struct itimerval it_val;	/* for setting itimer */
    it_val.it_value.tv_sec =     TIMESLICE/1000;
    it_val.it_value.tv_usec =    (TIMESLICE*1000) % 1000000;
    it_val.it_interval = it_val.it_value;
    if (setitimer(ITIMER_REAL, &it_val, NULL) == -1) {
        perror("error calling setitimer()");
        exit(1);
    }
}


void createContext(ucontext_t* threadContext) {
    getcontext(threadContext);
    threadContext->uc_link = NULL;
    threadContext->uc_stack.ss_sp = malloc(STACK_SIZE);
    threadContext->uc_stack.ss_size = STACK_SIZE;
    threadContext->uc_stack.ss_flags = 0;

    if (threadContext->uc_stack.ss_sp == NULL) {
       	printf("Error: Unable to allocate stack memory: %d bytes in the heap\n", STACK_SIZE);
        exit(1);
    }
}

void createSchedulerContext() {
    tcb* schedTCB = (tcb*) malloc(sizeof(tcb));
    schedTCB->TID = 0;
    createContext(&schedTCB->context);

    makecontext(&schedTCB->context, (void (*)()) &schedule, 0);

    put(map, SCHEDULER_THREAD, schedTCB);
}

void createMainContext() {
    tcb* mainTCB = (tcb*) malloc(sizeof(tcb));
    mainTCB->TID = 1;
    mainTCB->status = READY;

    put(map, MAIN_THREAD, mainTCB);
}
tcb* get_current_tcb() {
    return getFromHashMap(map, currentThreadTNum);
}

tcb* get_scheduler_tcb() {
    return getFromHashMap(map, SCHEDULER_THREAD);
}

int isLastQueue(int queueNum) {
    return queueNum >= QUEUE_NUM-1;
}

int isThreadInactive(int queueNum) {
    return peek(&threadQueue[queueNum])->status == FINISHED || peek(&threadQueue[queueNum])->status == BLOCKED_MUTEX || peek(&threadQueue[queueNum])->status == BLOCKED_JOIN;
}

void initializeQueue(Queue* q) {
    q->head = q->tail = NULL;
    q->_size = 0;
}

int isQueueEmpty(Queue* queue) {
    return queue->head == NULL;
}
int qSize(Queue* q) {
    return q->_size;
}

tcb* get(Queue* q, int i) {
    int count = 0;
    struct Node* curr = q->head;
    while (curr->next != NULL) {
        if (i == count)
            return curr->data;
        curr = curr->next;
        ++count;
    }
    if (i == count) {
        return q->tail->data;
    }

    return NULL;
}

void enqueue(Queue* q, tcb* x) {
    if (x == NULL) {
        return;
    }

    Node* tmp = (struct Node*)malloc(sizeof(struct Node));
    if (!tmp) {
        exit(EXIT_FAILURE);
    }

    tmp->data = x;

    if (q->tail == NULL) {
        q->head = q->tail = tmp;
        ++(q->_size);
        return;
    }

    q->tail->next = tmp;
    q->tail = tmp;
    q->tail->next = NULL;
    ++(q->_size);
}

tcb* dequeue(Queue* q) {
    if (q->head == NULL) {
        return NULL;
    }

    Node* tmp = q->head;
    tcb* res = tmp->data;
    q->head = q->head->next;

    if (q->head == NULL) {
        q->tail = NULL;
    }

    free(tmp);
    --(q->_size);
    return res;
}

tcb* peek(Queue* q) {
    if (q->head == NULL) {
        return NULL;
    }
    return q->head->data;
}

tcb* getTail(Queue* q) {
    if (q->tail == NULL) {
        return NULL;
    }
    return q->tail->data;
}

void clear(Queue* q) {
    Node* curr = q->head;
    Node* next = NULL;

    while (curr != NULL) {
        next = curr->next;
        free(curr);
        curr = next;
    }

    q->head = q->tail = NULL;
    q->_size = 0;
}

int hash(struct HashMap *map, int key) {
    return key % map->capacity;
}

void initHashMap(struct HashMap *map) {
    map->capacity = 205;
    map->msize = 0;
    map->arr = (struct HNode **)malloc(map->capacity * sizeof(struct HNode *));
    
    for (int i = 0; i < map->capacity; i++) {
        map->arr[i] = NULL;
    }

    map->dummy = (struct HNode *)malloc(sizeof(struct HNode));
    map->dummy->key = -1;
    map->dummy->value = NULL;

    map->initialized = 1;
}

void put(struct HashMap *map, int key, tcb* value) {
    struct HNode *tmp = (struct HNode *)malloc(sizeof(struct HNode));
    tmp->key = key;
    tmp->value = value;

    int hash_idx = hash(map, key);

    while (map->arr[hash_idx] != NULL && map->arr[hash_idx]->key != key && map->arr[hash_idx]->key != -1) {
        ++hash_idx;
        hash_idx %= map->capacity;
    }

    if (map->arr[hash_idx] == NULL || map->arr[hash_idx]->key == -1) {
        map->msize++;
    }

    map->arr[hash_idx] = tmp;
}

tcb* removeFromHashMap(HashMap *map, int key) {
    int hash_idx = hash(map, key);

    while (map->arr[hash_idx] != NULL) {
        if (map->arr[hash_idx]->key == key) {
            struct HNode *tmp = map->arr[hash_idx];
            map->arr[hash_idx] = map->dummy;
            --map->msize;
            return tmp->value;
        }
        ++hash_idx;
        hash_idx %= map->capacity;
    }

    return NULL;
}

tcb* getFromHashMap(struct HashMap *map, int key) {
    int hash_idx = hash(map, key);
    int counter = 0;

    while (map->arr[hash_idx] != NULL) {
        int counter = 0;
        if (counter++ > map->capacity) {
            return NULL;
        }

        if (map->arr[hash_idx]->key == key) {
            return map->arr[hash_idx]->value;
        }

        hash_idx++;
        hash_idx %= map->capacity;
    }

    return NULL;
}

int hashMapSize(struct HashMap *map) {
    return map->msize;
}

int isEmpty(struct HashMap *map) {
    return map->msize == 0;
}

