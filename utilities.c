#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utilities.h"

typedef struct {
    pid_t pid;
    int pipe_fd;
    char* source_dir;
    char* target_dir;
} Worker_Map;

static Worker_Map map[MAX_WORKERS];
static int count = 0;

void add_worker_to_map(pid_t pid, int pipe, char* source_dir, char* target_dir){
    if(count >= MAX_WORKERS){
        // worker map is full - ULIKELY BECAUSE OF 128 WORKERS, but still possible on extreme cases of n = 128 in manager startup
        return;
    }
    map[count].pid = pid;
    map[count].pipe_fd = pipe;
    map[count].source_dir = strdup(source_dir);
    map[count].target_dir = strdup(target_dir);
    count++;
}

int get_worker_pipe(pid_t pid){
    for(int i = 0; i < count; i++){
        if(map[i].pid == pid){
            return map[i].pipe_fd;
        }
    }
    // didn't find the pid in the map
    return -1;
}

void remove_worker_from_map(pid_t pid){
    for(int i = 0; i < count; i++){
        if(map[i].pid == pid){
            free(map[i].source_dir);
            free(map[i].target_dir);
            // to keep the continuity of the map, we can replace the pid and element with the last one
            map[i] = map[count - 1];
            count--;
            return;
        }
    }
}   

char* get_worker_source_dir(pid_t pid){
    for(int i = 0; i < count; i++){
        if(map[i].pid == pid){
            return map[i].source_dir;
        }
    }
    // didn't find the pid in the map
    return NULL;
}

char* get_worker_target_dir(pid_t pid){
    for(int i = 0; i < count; i++){
        if(map[i].pid == pid){
            return map[i].target_dir;
        }
    }
    // didn't find the pid in the map
    return NULL;
}