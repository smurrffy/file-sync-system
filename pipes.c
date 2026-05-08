# include <sys/types.h>
# include <sys/stat.h>
# include <fcntl.h>
# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <errno.h>
#include <unistd.h>


#include "pipes.h"

void create_named_pipe(const char* path){
    if(mkfifo(path, 0666) == -1){
        if(errno != EEXIST){
            perror("Error while creating named pipe");
            exit(1);
        }
    }
}

int open_named_pipe(const char* path, int flags){
    int fd = open(path, flags);
    if(fd < 0){
        perror("Error while opening pipe");
        exit(1);
    }
    return fd;
}

void write_to_pipe(int fd, const char* msg){
    // i am using a buffer in order to ensure that the pipe will always get MSGSIZE+1 characters, and i am initializing it to 0s
    char buf[MSGSIZE+1] = {0};
    strncpy(buf, msg, MSGSIZE);
    if(write(fd, buf, MSGSIZE+1) == -1){
        perror("Error while writing to pipe");
        exit(1);
    }
}

void read_from_pipe(int fd, char* buffer){
    if(read(fd, buffer, MSGSIZE+1) < 0){
        perror("Error while reading from pipe");
        exit(1);
    }
}

void close_pipe(int fd){
    close(fd);
}

void remove_named_pipe(const char* path){
    unlink(path);
}