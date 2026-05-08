#ifndef UTILITIES_H
#define UTILITIES_H

#include <sys/types.h>

#define MAX_WORKERS 128

void add_worker_to_map(pid_t, int, char*, char*);
int get_worker_pipe(pid_t);
void remove_worker_from_map(pid_t);
char* get_worker_source_dir(pid_t);
char* get_worker_target_dir(pid_t);

#endif