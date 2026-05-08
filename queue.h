#ifndef QUEUE_H
#define QUEUE_H

#include "list.h"

typedef struct queue Queue;

Queue *queue_create();
void enqueue(Queue *, char*, char*, char*, char*);
char** dequeue(Queue *);
int queue_is_empty(Queue* ); 
void queue_free(Queue* );

#endif