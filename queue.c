#include "queue.h"

#include <stdatomic.h>
#include <threads.h>


// Define the structure for a queue node
typedef struct Node{
    void *data;
    struct Node* next;
} Node;

typedef struct CvarNode{
    cnd_t *var;
    struct CvarNode* next;
} cvNode;

typedef struct CvarQueue{
    cvNode* front;
    cvNode* rear;

    size_t len;
} cvQueue;


// Define the structure for the queue
typedef struct Queue{
    Node* front;
    Node* rear;
    mtx_t lock;

    cvQueue *cvq;
    size_t size;
    size_t visited;
} Queue;



cvNode* createCvNode(cnd_t *newVar) {
    cvNode* newNode = (cvNode*)malloc(sizeof(cvNode));
    if (!newNode) {
        printf("Memory allocation error\n");
        exit(-1);
    }
    newNode->var = newVar;
    newNode->next = NULL;
    return newNode;
}

void addCV(cvQueue *cvq, cnd_t *newVar) {
    cvNode* newNode = createCvNode(newVar);
    cvq->len += 1;
    if (cvq->rear == NULL) {
        cvq->front = newNode;
        cvq->rear = newNode;
        return;
    }
    cvq->rear->next = newNode;
    cvq->rear = newNode;
}

cnd_t *rmCV(cvQueue* cvq) {
    if (cvq->front == NULL){
        return NULL;
    }
    cvNode* temp = cvq->front;
    cnd_t *var = temp->var;
    cvq->front = cvq->front->next;
    if (cvq->front == NULL){
        cvq->rear = NULL;
    }
    free(temp);
    cvq->len -= 1;
    return var;
}




Queue *q;


// Function to create a new node
Node* createNode(void *data) {
    Node* newNode = (Node*)malloc(sizeof(Node));
    if (!newNode) {
        printf("Memory allocation error\n");
        exit(-1);
    }
    newNode->data = data;
    newNode->next = NULL;
    return newNode;
}

// Function to create an empty queue
void initQueue(void){
    int rc;

    q = (Queue*)malloc(sizeof(Queue));
    if (!q){
        printf("Memory allocation error\n");
        exit(-1);
    }
    q->front = NULL;
    q->rear = NULL;

    rc = mtx_init(&(q->lock), mtx_plain);
    if (rc != thrd_success){
        printf("ERROR in mtx_init()\n");
        exit(-1);
    }

    q->size = 0;
    q->visited = 0;


    q->cvq = (cvQueue*)malloc(sizeof(cvQueue));
    if (!q->cvq){
        printf("Memory allocation error\n");
        exit(-1);
    }
    (q->cvq)->front = NULL;
    (q->cvq)->rear = NULL;
}

// Function to add an element to the queue
void enqueue(void *data){
    Node* newNode = createNode(data);
    cnd_t *CV;

    mtx_lock(&(q->lock));
    q->size += 1;

    if (q->rear == NULL) { /*Empty queue*/
        q->front = newNode;
        q->rear = newNode;
        return;
    }
    q->rear->next = newNode;
    q->rear = newNode;


    CV = rmCV(q->cvq);
    if(CV != NULL){
        cnd_signal(CV);
    }
    mtx_unlock(&(q->lock));
}

// Function to remove an element from the queue
void *dequeue(void){
    cnd_t CV;

    mtx_lock(&(q->lock));

    if((q->cvq)->front == NULL && q->front != NULL){
        Node* temp = q->front;
        void *data = temp->data;
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

    cnd_init(&CV);
    addCV(q->cvq, &CV);

    while((q->cvq)->front->var != &CV || q->front == NULL){ /*Wait for the condition variable's turn*/
        cnd_wait(&CV, &(q->lock));
    }

    Node* temp = q->front;
    void *data = temp->data;
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




bool tryDequeue(void **ret){
    if(q->front == NULL){
        return false;
    }    

    mtx_lock(&(q->lock));
    Node* temp = q->front;
    void *data = temp->data;
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


size_t size(void){
    return q->size;
}
size_t waiting(void){
    return (q->cvq)->len;
}
size_t visited(void){
    return q->visited;
}

// Function to destroy the queue and free all allocated memory
void destroyQueue(void){
    while (q->front != NULL){
        Node* temp = q->front;
        q->front = q->front->next;
        free(temp);
    }
    q->rear = NULL;
    mtx_destroy(&(q->lock));
    free(q);

    while ((q->cvq)->front != NULL){
        cvNode* temp = (q->cvq)->front;
        (q->cvq)->front = (q->cvq)->front->next;

        cnd_destroy(&(temp->var));
        free(temp);
    }
    (q->cvq)->rear = NULL;
    free(q->cvq);
}
