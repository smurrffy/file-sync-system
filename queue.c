#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "queue.h"

struct queue {
    List *list;
    int size;
};

Queue *queue_create(){
    Queue *q = malloc(sizeof(Queue));
    q->list = NULL;
    q->size = 0;
    return q;
}

void enqueue(Queue *q, char* source_dir, char* target_dir, char* filename, char* operation){
    q->list = list_append(q->list, source_dir, target_dir, filename, operation);
    q->size++;
}

char** dequeue(Queue *q){
    if(q->size == 0){
        return NULL;
    }
    char** data = malloc(4*sizeof(char*));
    data[0] = strdup(list_get_source(q->list));
    data[1] = strdup(list_get_target(q->list)); 
    data[2] = strdup(list_get_filename(q->list)); 
    data[3] = strdup(list_get_operation(q->list)); 
    List *node_to_free = q->list;
    q->list = list_get_next(q->list);
    q->size--;

    // 3. --- FIX START: Ελευθέρωση των δεδομένων ΤΟΥ ΚΟΜΒΟΥ ---
    // Πρέπει να ελευθερώσουμε τα strings που κρατούσε ο κόμβος της λίστας
    // γιατί έχουμε ήδη πάρει αντίγραφα (strdup) στο array 'data'
    free(list_get_source(node_to_free));
    free(list_get_target(node_to_free));
    free(list_get_filename(node_to_free));
    free(list_get_operation(node_to_free));
    // --- FIX END ---
    free(node_to_free);
    return data;
}

int queue_is_empty(Queue* q){
    if(q->size == 0)
        return 1;
    else
        return 0;
}

void queue_free(Queue* q){
    list_free(q->list);
    free(q);
}