#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"

struct list {
    char* source;
    char* target;
    char* filename;
    char* operation;
    List*    next;
};

List *list_first(List *list){
    return list;
}

List *list_last(List *list){
    if(list == NULL)
        return list;
        
    while(list->next != NULL){
        list = list->next;
    }
    return list;
}

char* list_get_source(List *list){
    return list->source;
}

char* list_get_target(List *list){
    return list->target;
}

char* list_get_filename(List *list){
    return list->filename;
}

char* list_get_operation(List *list){
    return list->operation;
}

List *list_get_next(List *list){
    return list->next;
}

List *list_append(List *list, char* source_dir, char* target_dir, char* fn, char* op){
    List* new_node= malloc(sizeof(List));

    new_node->source = strdup(source_dir);
    new_node->target = strdup(target_dir);
    new_node->filename = strdup(fn);
    new_node->operation = strdup(op);

    new_node->next = NULL;
    if(list == NULL){
        // if this is the first element, this is the list now
        list = new_node;
    }
    else{
        list_last(list)->next = new_node;
    }
    return list;
}

void list_free(List *list){
    List *next = NULL;
    while(list != NULL){
        next = list->next;
        free(list->source);
        free(list->target);
        free(list->filename);
        free(list->operation);
        free(list);
        list = next;
    }
}