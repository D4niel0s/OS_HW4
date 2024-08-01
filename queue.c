#include "queue.h"

#include <stdlib.h>
#include <threads.h>


/**
 * The general idea is to hold a queue for condition variables, in order to
 * maintain order of dequeue operations.
 * 
 * Each time an item is inserted, it wakes up the first thread waiting in dequeue,
 * and each time an item is popped, it adds itself to the condition variable queue,
 * in order to wait for it's turn.
 */



/* Structs for queue nodes */
typedef struct Node{
    void *data;
    struct Node* next;
} Node;

typedef struct CvarNode{
    cnd_t *var;
    struct CvarNode* next;
} cvNode;


/* This queue will be a field in the "main" queue - it will hold condition variables in the order they were created */
typedef struct CvarQueue{
    cvNode* front;
    cvNode* rear;

    size_t len;
} cvQueue;


/* "main" queue struct */
typedef struct Queue{
    Node* front;
    Node* rear;
    mtx_t lock;

    cvQueue *cvq;
    size_t size;
    size_t visited;
} Queue;



/* Functions for manipulating the condition variables queue*/
cvNode* createCvNode(cnd_t *newVar) {
    cvNode* newNode = (cvNode*)malloc(sizeof(cvNode));
    
    newNode->var = newVar;
    newNode->next = NULL;
    return newNode;
}

void addCV(cvQueue *cvq, cnd_t *newVar) {
    cvNode* newNode = createCvNode(newVar);
    cvq->len += 1;
    if (cvq->rear == NULL){
        cvq->front = newNode;
        cvq->rear = newNode;

        return;
    }
    cvq->rear->next = newNode;
    cvq->rear = newNode;
}

cnd_t *rmCV(cvQueue* cvq) {
    if(cvq->front == NULL){
        return NULL;
    }
    cvNode* temp = cvq->front;
    cnd_t *var = temp->var;

    cvq->front = cvq->front->next;
    if(cvq->front == NULL){
        cvq->rear = NULL;
    }
    free(var); /* var is allocated in dequeue */
    free(temp);
    
    cvq->len -= 1;
    return var;
}



/* The "main" queue is declared here as a global */
Queue *q;





/* Helper function */
Node* createNode(void *data) {
    Node* newNode = (Node*)malloc(sizeof(Node));
    
    newNode->data = data;
    newNode->next = NULL;
    return newNode;
}

/* Allocate and initialize all data structures */
void initQueue(void){
    q = (Queue*)malloc(sizeof(Queue));
    
    q->front = NULL;
    q->rear = NULL;

    mtx_init(&(q->lock), mtx_plain);

    q->size = 0;
    q->visited = 0;
    q->cvq = (cvQueue*)malloc(sizeof(cvQueue));

    (q->cvq)->front = NULL;
    (q->cvq)->rear = NULL;
    (q->cvq)->len = 0;
}


/* Again, the idea is to wake up the first waiting dequeue operation - the front of the cvQueue*/
void enqueue(void *data){
    cnd_t *CV;
    Node* newNode = createNode(data);

    mtx_lock(&(q->lock));
    
    q->size += 1;

    if((q->front) == NULL){
        q->front = newNode;
        q->rear = newNode;

        CV = rmCV(q->cvq);

        mtx_unlock(&(q->lock));
        /* Wake up the waiting thread */
        if(CV != NULL){
            cnd_signal(CV);
        }
        return;
    }

    q->rear->next = newNode;
    q->rear = newNode;

    /* After adding an item to the queue, get the first item waiting for dequeue */
    CV = rmCV(q->cvq);
    mtx_unlock(&(q->lock));

    /* Wake up the waiting thread */
    if(CV != NULL){
        cnd_signal(CV);
    }
}

/* Again, the idead is to add the "request" to the cvQueue and wait for it's turn. If there are no requests and the queue is not empty, we just pop an item from the queue*/
void *dequeue(void){
    cnd_t *CV;
    Node *temp;
    void *data;

    mtx_lock(&(q->lock));

    if((q->cvq)->front == NULL && q->front != NULL){
        /* Remove an item from the queue as ususal */
        temp = q->front;
        data = temp->data;
        q->front = q->front->next;
        if (q->front == NULL){
            q->rear = NULL;
        }
        free(temp);

        q->size -= 1;
        q->visited +=1;

        mtx_unlock(&(q->lock));
        return data;
    }

    /* Add a condition variable to the queue */
    CV = (cnd_t *)malloc(sizeof(cnd_t));
    cnd_init(CV);
    addCV(q->cvq, CV);

    while(q->front == NULL || (q->cvq)->front != NULL){ /*Wait for the condition variable's turn*/
        cnd_wait(CV, &(q->lock));
    }
    
    /* Remove an item from the queue */
    temp = q->front;
    data = temp->data;
    q->front = q->front->next;
    if (q->front == NULL){
        q->rear = NULL;
    }
    free(temp);

    q->size -= 1;
    q->visited +=1;

    mtx_unlock(&(q->lock));
    return data;
}


/* Here it's a normal dequeue with a mutex around it*/
bool tryDequeue(void **ret){
    Node* temp;
    void *data;

    if(q->front == NULL){
        return false;
    }    

    mtx_lock(&(q->lock));
    temp = q->front;
    data = temp->data;
    q->front = q->front->next;
    if (q->front == NULL){
        q->rear = NULL;
    }
    free(temp);

    q->size -= 1;
    q->visited +=1;
    mtx_unlock(&(q->lock));

    *ret = data;
    return true;
}

/* The values are maintained in the corresponding structs */
size_t size(void){
    return q->size;
}
size_t waiting(void){
    return (q->cvq)->len;
}
size_t visited(void){
    return q->visited;
}


/* Free all allocated memory and destroy mutex and condition variables */
void destroyQueue(void){
    cvNode* temp2;
    Node* temp1;

    while (q->front != NULL){
        temp1 = q->front;
        q->front = q->front->next;
        free(temp1);
    }
    q->rear = NULL;
    mtx_destroy(&(q->lock));
    free(q);

    while ((q->cvq)->front != NULL){
        temp2 = (q->cvq)->front;
        (q->cvq)->front = (q->cvq)->front->next;

        cnd_destroy(temp2->var);
        free(temp2);
    }
    (q->cvq)->rear = NULL;
    free(q->cvq);
}
