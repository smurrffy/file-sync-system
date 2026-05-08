#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <time.h>

#include "sync_info_mem_store.h"

struct list{
    SyncInfo* info;
    Hashtable_list *next;
};

struct hashtable {
    size_t size;
    Hashtable_list** table;
};

Hashtable_list *hashtable_list_push(Hashtable_list *l, SyncInfo* info){
    Hashtable_list *node = malloc(sizeof(Hashtable_list));
    node->next = NULL;
    node->info = info;
    if(l == NULL)
        l = node;
    else{
        Hashtable_list *cur = l;
        while(cur->next!=NULL)
            cur = cur->next;
        cur->next = node;
    }
    return l;
}

int hashtable_list_size(Hashtable_list *l){
    Hashtable_list *cur = l;
    int count = 0;
    while(cur!=NULL){
        count++;
        cur = cur->next;
    }
    return count;
}

void hashtable_list_free(Hashtable_list* l){
    Hashtable_list* cur = NULL;
    while (l != NULL) {
        cur = l->next;
        if (l->info != NULL) {
            if (l->info->source_dir) free(l->info->source_dir);
            if (l->info->target_dir) free(l->info->target_dir);
            if (l->info->last_sync_time) free(l->info->last_sync_time);

            free(l->info);
        }
        free(l);
        l = cur;
    }
}

SyncInfo* hashtable_list_get_info(Hashtable_list *l){
    return l->info;
}

Hashtable_list *hashtable_list_get_next(Hashtable_list *l){
    return l->next;
}

Hashtable_list *hashtable_list_delete_info(Hashtable_list* l, char* source_dir) {
    Hashtable_list* cur = l;
    Hashtable_list* prev = NULL;
    while (cur != NULL) {
        if (strcmp(cur->info->source_dir, source_dir) == 0) {
            
            // --- FIX START: Ελευθέρωση μνήμης του περιεχομένου ---
            if(cur->info != NULL) {
                free(cur->info->source_dir);
                free(cur->info->target_dir);
                if(cur->info->last_sync_time) free(cur->info->last_sync_time);
                free(cur->info);
            }
            // --- FIX END ---

            if (prev == NULL) { // Ήταν ο πρώτος κόμβος
                Hashtable_list* next = cur->next;
                free(cur);
                return next;
            } else { // Ήταν ενδιάμεσος ή τελευταίος
                prev->next = cur->next;
                free(cur);
                return l;
            }
        }
        prev = cur;
        cur = cur->next;
    }
    return l;
}

SyncInfo* hashtable_list_search(Hashtable_list *l, char* source_dir) {
    Hashtable_list* cur = l;
    while (cur != NULL) {
        if (strcmp(cur->info->source_dir, source_dir) == 0)
            return cur->info;
        cur = cur->next;
    }
    return NULL;
}

// create a new hashtable with the specified size
Hashtable* hashtable_create(size_t size) {
    Hashtable *h = malloc(sizeof(Hashtable));
    h->size = size;
    h->table = malloc(sizeof(Hashtable_list*) * size);
    for (int i = 0; i < size; i++)
        h->table[i] = NULL;
    return h;
}

// create a new hashtable with the default size
Hashtable* hashtable_with_default_size() {
    return hashtable_create(HASHTABLE_SIZE);
}

void hashtable_destroy(Hashtable* h) {
    for (int i = 0; i < hashtable_size(h); i++)
        hashtable_list_free(h->table[i]);
    free(h->table);
    free(h);
}

// Hash function
size_t hashtable_hash(Hashtable* h, char* source_dir) {
    size_t hash = 0;
    for(size_t i=0; i<strlen(source_dir); i++ ){
        hash += source_dir[i];
    }
    return hash%(h->size);
}

Hashtable* hashtable_insert(Hashtable* h, SyncInfo* info) {
    size_t key = hashtable_hash(h, info->source_dir); 
    float lf = hashtable_get_load_factor(h);
    if(lf > 0.75){
        h = hashtable_resize(h);
        key = hashtable_hash(h, info->source_dir);
    }
    h->table[key] = hashtable_list_push(h->table[key], info);
    return h;
}

Hashtable* hashtable_resize(Hashtable* h) {
    Hashtable* new_ht = NULL;
    size_t new_size = next_prime(2*(h->size));
    new_ht = hashtable_create(new_size);

    for(int i=0; i < h->size; i++){
        Hashtable_list* cur = h->table[i];
        while(cur != NULL){
            // 1. Βάζουμε τα δεδομένα στο νέο hashtable
            new_ht = hashtable_insert(new_ht, cur->info);
            
            // 2. Κρατάμε τον τρέχοντα κόμβο για διαγραφή
            Hashtable_list* temp = cur;
            
            // 3. Προχωράμε στον επόμενο
            cur = cur->next;

            // 4. Ελευθερώνουμε ΜΟΝΟ τον κόμβο της παλιάς λίστας
            // ΠΡΟΣΟΧΗ: ΔΕΝ πειράζουμε το temp->info γιατί το χρησιμοποιεί το new_ht
            free(temp); 
        }  
    }

    // Ελευθερώνουμε τον πίνακα δεικτών και το struct του παλιού hashtable
    // Χωρίς να καλέσουμε την hashtable_destroy που θα διέλυε τα πάντα
    free(h->table);
    free(h);

    return new_ht;
}

SyncInfo* hashtable_get(Hashtable* h, char* source_dir) {
    SyncInfo* info = NULL;
    size_t key = hashtable_hash(h, source_dir); 
    info = hashtable_list_search(h->table[key], source_dir);
    if(info != NULL){
        return info;
    }
    return NULL;
}

STATUS hashtable_remove(Hashtable* h, char* source_dir) {
    SyncInfo* info = NULL;
    size_t key = hashtable_hash(h, source_dir); 
    info = hashtable_list_search(h->table[key], source_dir);
    if(info != NULL){
        h->table[key] = hashtable_list_delete_info(h->table[key], info->source_dir);
        return SUCCESS;
    }
    return FAILURE;
}

size_t hashtable_get_number_of_entries(Hashtable* h) {
    size_t count = 0;
    for (int i = 0; i < hashtable_size(h); i++) 
        count += hashtable_list_size(h->table[i]);
    return count;
}

float hashtable_get_load_factor(Hashtable* h) {
    return (float) hashtable_get_number_of_entries(h) / hashtable_size(h);
}

size_t hashtable_size(Hashtable* h) {
    return h->size;
}

size_t next_prime(size_t n) {
    size_t i = n + 1;
    while (1) {
        int is_prime = 1;
        for (int j = 2; j < i; j++) {
            if (i % j == 0) {
                is_prime = 0;
                break;
            }
        }
        if (is_prime)
            return i;
        i++;
    }
}

SyncInfo* hashtable_get_by_wd(Hashtable *h, int wd){
    for(int i = 0; i < h->size; i++){
        Hashtable_list* cur = h->table[i];
        while (cur != NULL)
        {
            if(cur->info->wd == wd){
                return cur->info;
            }
            cur = cur->next;
        }
        
    }
    // wd not found
    return NULL;
}