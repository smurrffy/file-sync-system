#ifndef SYNC_INFO_MEM_STORE_H
#define SYNC_INFO_MEM_STORE_H

#include <stdio.h>

#define HASHTABLE_SIZE 101
#define MAX_LOAD_FACTOR 0.75

enum status {SUCCESS, FAILURE};
typedef enum status STATUS;

struct sync_info{
    char* source_dir;
    char* target_dir;
    int wd;
    char* last_sync_time;
    int error_count;
    int is_active; // status: 1 if it is active, 0 if not
};

typedef struct sync_info SyncInfo;
typedef struct list Hashtable_list;
typedef struct hashtable Hashtable;

int hastable_list_size(Hashtable_list *l);
void hastable_list_free(Hashtable_list* l);
Hashtable_list *hastable_list_get_next(Hashtable_list *l);
Hashtable_list *hastable_list_push(Hashtable_list *l, SyncInfo* customer);
SyncInfo* hastable_list_get_info(Hashtable_list *l);
SyncInfo* hastable_list_search(Hashtable_list *l, char* id);
Hashtable_list * hastable_list_delete_info(Hashtable_list* l, char* id);

Hashtable* hashtable_create(size_t size);
void hashtable_destroy(Hashtable* h);
size_t hashtable_size(Hashtable* h);
Hashtable* hashtable_insert(Hashtable* h, SyncInfo* c);
STATUS hashtable_remove(Hashtable* h, char* id);
SyncInfo* hashtable_get(Hashtable* h, char* id);
size_t hashtable_hash(Hashtable* h, char* id);
Hashtable* hashtable_with_default_size();
size_t hashtable_get_number_of_entries(Hashtable* h);
float hashtable_get_load_factor(Hashtable* h);
Hashtable* hashtable_resize(Hashtable* h);
size_t next_prime(size_t n);
SyncInfo* hashtable_get_by_wd(Hashtable *h, int wd);

#endif