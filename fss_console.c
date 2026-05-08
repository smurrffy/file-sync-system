#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>

#include "pipes.h"

#define FSS_IN "fss_in"
#define FSS_OUT "fss_out"

void get_timestamp(char* , size_t);

int main(int argc, char** argv){
    char* logfile = NULL;
    if(argc == 3 && strcmp(argv[1], "-l") == 0){
        logfile = argv[2];
    } 
    else{
        perror("Unknown or incomplete argument provided on startup. Usage: ./fss_console -l <console_logfile>");
        exit(1);
    }
    // check if any of the files wasn't provided correctly
    if(logfile == NULL){
        perror("Invalid logfile");
        exit(1);
    }
    FILE *lf;
    if((lf = fopen(logfile, "w")) == NULL){
        perror("opening the logfile");
        exit(1);
    }

    int fss_in = open_named_pipe(FSS_IN, O_RDWR | O_NONBLOCK);
    int fss_out = open_named_pipe(FSS_OUT, O_RDONLY | O_NONBLOCK);

    // start the poll for either a new command or a command coming back from the fss_manager
    struct pollfd poll_fd[2];
    poll_fd[0].fd = STDIN_FILENO;
    poll_fd[0].events = POLLIN;
    poll_fd[1].fd = fss_out;
    poll_fd[1].events = POLLIN;

    char buffer[512];
    int running = 1;

    while(running){
        fflush(stdout);
        poll(poll_fd, 2, -1);
        
        if(poll_fd[0].revents & POLLIN){
            // a console command has been requested
            if(fgets(buffer, sizeof(buffer), stdin) == NULL){
                perror("fgets");
                continue;
            }
            int length = strlen(buffer);
            if(buffer[length-1] != '\n'){
                // we append a \n to the lines that dont have it
                buffer[length] = '\n';
                buffer[length + 1] = '\0';
            }

            char timestamp[64];
            get_timestamp(timestamp, sizeof(timestamp));
            fprintf(lf, "%s Command %s", timestamp, buffer);
            fflush(lf);

            write_to_pipe(fss_in, buffer);
            
        }

        if(poll_fd[1].revents & POLLIN){
            // if we get a message back from the manager
            char response[MSGSIZE+1] = {0};
            int n = read(fss_out, response, MSGSIZE+1);
            if(n > 0){
                if(strstr(response, "Manager shutdown complete.") != NULL){
                    // if the requested command was shutdown, after the manager has shutdown, we can shutdown the console too
                    char timestamp[64];
                    get_timestamp(timestamp, sizeof(timestamp));
                    printf("%s Manager shutdown complete.\n", timestamp);
                    running = 0;
                }
                else{
                    printf("%s", response);
                }
            }
        }
    }

    close_pipe(fss_in);
    close_pipe(fss_out);

    return 0;
}

void get_timestamp(char* buffer, size_t size) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    strftime(buffer, size, "[%Y-%m-%d %H:%M:%S]", tm_info);
}