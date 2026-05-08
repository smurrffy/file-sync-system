#ifndef PIPES_H
#define PIPES_H

#define MSGSIZE 256

void create_named_pipe(const char*);
int open_named_pipe(const char*, int);
void write_to_pipe(int, const char*);
void read_from_pipe(int, char*);
void close_pipe(int);
void remove_named_pipe(const char*);

#endif