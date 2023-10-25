// File:	thread-worker.c

// List all group member's name: Ghautham Sambabu, Abid Azad
// username of iLab: ilab2
// iLab Server:

#include "thread-worker.h"

//Global counter for total context switches and 
//average turn around and response time
long tot_cntx_switches=0;
double avg_turn_time=0;
double avg_resp_time=0;

#define SCHEDULER_THREAD 0
#define MAIN_THREAD 1
#define QUEUE_NUM 4
#define QUANTUM 10
int threadCounter = 2; 
Queue threadQueue[QUEUE_NUM];
int started = 1;
HashMap *map = NULL;
int isSchedCreated = 0;
int isYielding = 0;
uint currentThreadTNum = MAIN_THREAD;
int currentThreadQNum=0;
enum sched_options {psjf, mlfq};
#ifndef MLFQ
	int SCHED = psjf;
#else 
	int SCHED = mlfq;
#endif
int completedThreads = 0;
int scheduledThreads = 0;

// create a new thread 
int worker_create(worker_t * thread, pthread_attr_t * attr, void *(*function)(void*), void * arg) {	
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
    *thread = threadCounter;
    // - create Thread Control Block (TCB)
    tcb* newThread = (tcb*) malloc(sizeof(tcb));
    // - create and initialize the context of this worker thread
    newThread->TID = threadCounter;
    newThread->status = READY;
    newThread->joiningThread = 0;
    newThread->elapsed = 0;
    newThread->beenScheduledOnce=0;
    gettimeofday(&newThread->start_time, NULL);
    // - allocate space of stack for this thread to run
    createContext(&newThread->context);
    getcontext(&newThread->context);   
    /*
    After everything is set, push this thread into run queue 
    and make it ready for the execution.
    */ 
    makecontext(&newThread->context, (void (*)()) &worker_start, 3, newThread, function, arg);
    put(map, threadCounter++, newThread);
    if (!isSchedCreated) {
        tcb* schedTCB = (tcb*) malloc(sizeof(tcb));
        schedTCB->TID = 0;
        gettimeofday(&schedTCB->start_time, NULL);
        schedTCB->beenScheduledOnce = 0;
        createContext(&schedTCB->context);
        makecontext(&schedTCB->context, (void (*)()) &schedule, 0);
        put(map, SCHEDULER_THREAD, schedTCB);
        tcb* mainTCB = (tcb*) malloc(sizeof(tcb));
        mainTCB->TID = 1;
        mainTCB->status = READY;
        gettimeofday(&mainTCB->start_time, NULL);
        mainTCB->beenScheduledOnce = 0;
        put(map, MAIN_THREAD, mainTCB);
        enqueue(&threadQueue[0], getFromHashMap(map, MAIN_THREAD));
        enqueue(&threadQueue[0], newThread);
        isSchedCreated = 1;
        setupTimer();
    } else {
        enqueue(&threadQueue[0], newThread);
    }
    return 0;
}

void worker_start(tcb *currTCB, void (*function)(void *), void *arg) {
    struct timeval start_time, end_time;
    gettimeofday(&currTCB->start_time, NULL);
    function(arg); 
    currTCB->status = FINISHED;
    free(currTCB->context.uc_stack.ss_sp);
    setcontext(&getFromHashMap(map, SCHEDULER_THREAD)->context);
}

/* give CPU possession to other user-level worker threads voluntarily */
int worker_yield() {
	/*
     - change worker thread's state from Running to Ready
	 - save context of this thread to its thread control block
	 - switch from thread context to scheduler context
    */ 
	tcb* currTCB = getFromHashMap(map, currentThreadTNum);
    currTCB->elapsed += QUANTUM;
    currTCB->status = READY;
    isYielding = 1;
    swapcontext(&currTCB->context, &getFromHashMap(map, SCHEDULER_THREAD)->context);
	tot_cntx_switches++;
	return 0;
};

/* terminate a thread */
void worker_exit(void *value_ptr) {
	// - de-allocate any dynamic memory created when starting this thread
	tcb* currTCB = getFromHashMap(map, currentThreadTNum);
    currTCB->retVal = value_ptr;
    free(currTCB->context.uc_stack.ss_sp);
    if (currTCB->joiningThread != 0) {
        tcb* joinedTCB = getFromHashMap(map, currTCB->joiningThread);
        joinedTCB->status = READY;
        enqueue(&threadQueue[0],joinedTCB);
    }
    gettimeofday(&currTCB->end_time, NULL);
    avg_turn_time = ((avg_turn_time * completedThreads + ((double)(((double)(currTCB->end_time.tv_sec - currTCB->start_time.tv_sec) * 1000) + ((double)(currTCB->end_time.tv_usec - currTCB->start_time.tv_usec)) / 1000))) / (completedThreads + 1));
    completedThreads++;
    currTCB->status = FINISHED;
    setcontext(&getFromHashMap(map, SCHEDULER_THREAD)->context);	
};

/* Wait for thread termination */
int worker_join(worker_t thread, void **value_ptr) {	
	/*
     - wait for a specific thread to terminate
	 - de-allocate any dynamic memory created by the joining thread
    */ 
	tcb* currTCB = getFromHashMap(map, currentThreadTNum);
    tcb* joinedTCB = getFromHashMap(map, thread);
    if (joinedTCB->status != FINISHED) {
        joinedTCB->joiningThread = currTCB->TID;
        currTCB->status = BLOCKED_JOIN;
        swapcontext(&currTCB->context, &getFromHashMap(map, SCHEDULER_THREAD)->context);
        tot_cntx_switches++;    
    }
    if(value_ptr != NULL) {
        *value_ptr = joinedTCB->retVal;
    }
	return 0;
};

/* initialize the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, 
                          const pthread_mutexattr_t *mutexattr) {
	//- initialize data structures for this mutex
	return 0;
};

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex) {
        /*
           - use the built-in test-and-set atomic function to test the mutex
           - if the mutex is acquired successfully, enter the critical section
           - if acquiring mutex fails, push current thread into block list and
           context switch to the scheduler thread
        */ 
		while (atomic_flag_test_and_set(&mutex->flag)) {
			tcb* currTCB = getFromHashMap(map, currentThreadTNum);
			enqueue(&(mutex->threadQueue), currTCB);
			currTCB->status = BLOCKED_MUTEX;
			swapcontext(&currTCB->context, &getFromHashMap(map, SCHEDULER_THREAD)->context);
            tot_cntx_switches++;
        }
		mutex->owner = getFromHashMap(map, currentThreadTNum)->TID;
        return 0;
};

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex) {
	// - release mutex and make it available again. 
	// - put threads in block list to run queue 
	// so that they could compete for mutex later.
    if(getFromHashMap(map, currentThreadTNum)->TID != mutex->owner) {
        exit(1);
    }
    if(!isQueueEmpty(&(mutex->threadQueue))) {
        for (int i = 0; i < queueSize(&(mutex->threadQueue)); i++) {
            tcb* x = queueGet(&(mutex->threadQueue), i);
            getFromHashMap(map, x->TID)->status = READY;
            enqueue(&threadQueue[0], getFromHashMap(map, x->TID));
        }
        queueClear(&(mutex->threadQueue));
    }
    atomic_flag_clear_explicit(&mutex->flag, memory_order_seq_cst);
	return 0;
};

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex) {
	// - de-allocate dynamic memory created in worker_mutex_init
    queueClear(&(mutex->threadQueue));
	return 0;
};

/* scheduler */
static void schedule() {
	// - invoke scheduling algorithms according to the policy (PSJF or MLFQ)
	 if (SCHED == psjf)
			sched_psjf();
	 else if (SCHED == mlfq)
	 		sched_mlfq();
}

/* Pre-emptive Shortest Job First (POLICY_PSJF) scheduling algorithm */
static void sched_psjf() {
    // Get the current running thread
    tcb* moveToBack = NULL;
    if (isThreadInactive(0)) {
        dequeue(&threadQueue[0]);
    } else if (isYielding || isLastQueue(0)) {
        moveToBack= dequeue(&threadQueue[0]);
    } 
    isYielding = 0;
    int numThreads = 0; 
    tcb** threads = malloc(sizeof(tcb*) * (&threadQueue[0])->queueSize);
    while (!isQueueEmpty(&threadQueue[0])) {
        threads[numThreads++] = dequeue(&threadQueue[0]);
    }
    tcb* minElapsedThread = NULL;
    for (int i = 0; i < numThreads; i++) {
        if (minElapsedThread == NULL || threads[i]->elapsed < minElapsedThread->elapsed) {
            minElapsedThread = threads[i];
        }
    }
    if(moveToBack!=NULL)
        enqueue(&threadQueue[0], moveToBack);       
    for (int i = 0; i < numThreads; i++) {
        if (threads[i] != minElapsedThread) {
            enqueue(&threadQueue[0], threads[i]);
        }
    }
    free(threads);
    if(minElapsedThread != NULL){
        minElapsedThread->elapsed+=QUANTUM;
        enqueue(&threadQueue[0], minElapsedThread);
        tcb* currTCB = peek(&threadQueue[0]);
        currentThreadTNum = currTCB->TID;
        setcontext(&currTCB->context);
        tot_cntx_switches++;
    }
}

/* Preemptive MLFQ scheduling algorithm */
static void sched_mlfq() {
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
		if (isQueueEmpty(&threadQueue[queueNum])){queueNum++; continue;}
        tcb* currTCB = peek(&threadQueue[queueNum]);
        currentThreadTNum = currTCB->TID;
        currentThreadQNum = queueNum;
        setcontext(&currTCB->context);
        queueNum++;
    }
}

//DO NOT MODIFY THIS FUNCTION
/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void) {
       fprintf(stderr, "Total context switches %ld \n", tot_cntx_switches);
       fprintf(stderr, "Average turnaround time %lf \n", avg_turn_time);
       fprintf(stderr, "Average response time  %lf \n", avg_resp_time);
}

void timer_handler(int signum) {
    if (currentThreadTNum != SCHEDULER_THREAD) {
        swapcontext(&getFromHashMap(map, currentThreadTNum)->context, &getFromHashMap(map, SCHEDULER_THREAD)->context);
        if(getFromHashMap(map, SCHEDULER_THREAD)->beenScheduledOnce == 0){
            gettimeofday(&getFromHashMap(map, SCHEDULER_THREAD)->end_time, NULL);
            avg_resp_time = ((avg_resp_time * scheduledThreads + ((double)(((double)(getFromHashMap(map, SCHEDULER_THREAD)->end_time.tv_sec - getFromHashMap(map, SCHEDULER_THREAD)->start_time.tv_sec) * 1000) + ((double)(getFromHashMap(map, SCHEDULER_THREAD)->end_time.tv_usec - getFromHashMap(map, SCHEDULER_THREAD)->start_time.tv_usec) / 1000)))) / (scheduledThreads + 1));
            scheduledThreads++;
            getFromHashMap(map, SCHEDULER_THREAD)->beenScheduledOnce = 1;
        }
        tot_cntx_switches++;
    }
}

void setupTimer() {
    struct itimerval timer;	
    if (signal(SIGALRM, timer_handler) == SIG_ERR) {
        exit(1);
    }
    timer.it_value.tv_sec = QUANTUM/1000;
    timer.it_value.tv_usec = (QUANTUM*1000) % 1000000;
    timer.it_interval = timer.it_value;
    if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
        exit(1);
    }
    int started = 0;
    while (!started) {
        pause();
        started = 1;
    }
}

void stopTimer() {
    struct itimerval timer;
    timer.it_interval.tv_usec = 0;
    timer.it_interval.tv_sec = 0;
    timer.it_value.tv_usec = 0;
    timer.it_value.tv_sec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);
}

void startTimer() {
    struct itimerval timer;	
    timer.it_value.tv_sec = QUANTUM/1000;
    timer.it_value.tv_usec = (QUANTUM*1000) % 1000000;
    timer.it_interval = timer.it_value;
    if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
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
        exit(1);
    }
}

int isLastQueue(int queueNum) {
    return queueNum >= QUEUE_NUM-1;
}

int isThreadInactive(int queueNum) {
    return peek(&threadQueue[queueNum])->status == FINISHED || peek(&threadQueue[queueNum])->status == BLOCKED_MUTEX || peek(&threadQueue[queueNum])->status == BLOCKED_JOIN;
}

void initializeQueue(Queue* startingQueue) {
    startingQueue->front = startingQueue->back = NULL;
    startingQueue->queueSize = 0;
}

int isQueueEmpty(Queue* queue) {
    return queue->front == NULL;
}
int queueSize(Queue* queue) {
    return queue->queueSize;
}

tcb* queueGet(Queue* queue, int position) {
    int count = 0;
    struct Node* curr = queue->front;
    while (curr->next != NULL) {
        if (position == count)
            return curr->data;
        curr = curr->next;
        ++count;
    }
    if (position == count) {
        return queue->back->data;
    }

    return NULL;
}

void enqueue(Queue* queue, tcb* thread) {
    if (queue == NULL) {
        return;
    }
    Node* temp = (struct Node*)malloc(sizeof(struct Node));
    if (!temp) {
        exit(EXIT_FAILURE);
    }
    temp->data = thread;
    if (queue->back == NULL) {
        queue->front = queue->back = temp;
        ++(queue->queueSize);
        return;
    }
    queue->back->next = temp;
    queue->back = temp;
    queue->back->next = NULL;
    ++(queue->queueSize);
}

tcb* dequeue(Queue* queue) {
    if (queue->front == NULL) {
        return NULL;
    }
    Node* temp = queue->front;
    tcb* popped = temp->data;
    queue->front = queue->front->next;
    if (queue->front == NULL) {
        queue->back = NULL;
    }
    free(temp);
    --(queue->queueSize);
    return popped;
}

tcb* peek(Queue* queue) {
    if (queue->front == NULL) {
        return NULL;
    }
    return queue->front->data;
}

void queueClear(Queue* queue) {
    Node* curr = queue->front;
    Node* next = NULL;

    while (curr != NULL) {
        next = curr->next;
        free(curr);
        curr = next;
    }

    queue->front = queue->back = NULL;
    queue->queueSize = 0;
}

int hash(struct HashMap *map, int key) {
    return key % map->hashMapCapacity;
}

void initHashMap(struct HashMap *map) {
    map->hashMapCapacity = 300;
    map->hashMapSize = 0;
    map->arr = (struct HNode **)malloc(map->hashMapCapacity * sizeof(struct HNode *));
    for (int i = 0; i < map->hashMapCapacity; i++) {
        map->arr[i] = NULL;
    }
    map->dummy = (struct HNode *)malloc(sizeof(struct HNode));
    map->dummy->key = -1;
    map->dummy->value = NULL;
    map->initialized = 1;
}

void put(struct HashMap *map, int key, tcb* value) {
    struct HNode *temp = (struct HNode *)malloc(sizeof(struct HNode));
    temp->key = key;
    temp->value = value;
    int hashIndex = hash(map, key);
    while (map->arr[hashIndex] != NULL && map->arr[hashIndex]->key != key && map->arr[hashIndex]->key != -1) {
        ++hashIndex;
        hashIndex %= map->hashMapCapacity;
    }
    if (map->arr[hashIndex] == NULL || map->arr[hashIndex]->key == -1) {
        map->hashMapSize++;
    }
    map->arr[hashIndex] = temp;
}

tcb* getFromHashMap(struct HashMap *map, int key) {
    int hashIndex = hash(map, key);
    int counter = 0;
    while (map->arr[hashIndex] != NULL) {
        int counter = 0;
        if (counter++ > map->hashMapCapacity) {
            return NULL;
        }
        if (map->arr[hashIndex]->key == key) {
            return map->arr[hashIndex]->value;
        }
        hashIndex++;
        hashIndex %= map->hashMapCapacity;
    }
    return NULL;
}

int hashMapSize(struct HashMap *map) {
    return map->hashMapSize;
}


