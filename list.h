#ifndef LIST_H
#define LIST_H

typedef struct list List;

List *list_first(List *);
List *list_last(List *);
char* list_get_source(List *);
char* list_get_target(List *);
char* list_get_filename(List *);
char* list_get_operation(List *);
List *list_get_next(List *);
List *list_append(List *, char*, char*, char*, char*);
void list_free(List *);

#endif