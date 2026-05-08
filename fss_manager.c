#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/inotify.h>
#include <time.h>
#include <errno.h>
#include <poll.h>

#include "pipes.h"
#include "queue.h"
#include "list.h"
#include "utilities.h"
#include "sync_info_mem_store.h"

#define DEFAULT_WORKER_LIMIT 5

#define FSS_IN "fss_in"
#define FSS_OUT "fss_out"

#define MAX_LINE_LENGTH 256

#define INOTIFY_EVENT_SIZE  ( sizeof(struct inotify_event) )
#define INOTIFY_BUF_LEN     ( 1024 * (INOTIFY_EVENT_SIZE + 16) )

void shutdown(int, int, FILE*, FILE*);
void fork_worker(char*, char*, char*, char*, Hashtable*, FILE *);
void sighandler_worker(int);
void worker_funeral(Hashtable*, FILE *);
void get_timestamp(char* , size_t);
void handle_inotify_events(int, Hashtable*, Queue* , int, FILE *);
void empty_work_queue(Hashtable*, Queue*, int, FILE *);
void handle_console_commands(int, int, Hashtable*, Queue*, int , int , int *, FILE *);
int get_error_count(char* );

// variable that will be keeping track of the currently active workers
int active_workers = 0;

volatile sig_atomic_t child_dead = 0;

int main(int argc, char **argv){
    
    // setting up the sigaction for workers who terminate
    struct sigaction sa;
    sa.sa_handler = sighandler_worker;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if(sigaction(SIGCHLD, &sa, NULL) == -1){
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    // creating the named pipes to communicate with fss_console
    create_named_pipe(FSS_IN);
    create_named_pipe(FSS_OUT);

    char* logfile = NULL;
    char* config_file = NULL;
    int worker_limit = DEFAULT_WORKER_LIMIT;

    // reading the arguments
    for(int i = 1; i < argc; i++){
        if(strcmp(argv[i], "-l") == 0 && i + 1 < argc){
            logfile = argv[++i];
        } 
        else if(strcmp(argv[i], "-c") == 0 && i + 1 < argc){
            config_file = argv[++i];
        } 
        else if(strcmp(argv[i], "-n") == 0 && i + 1 < argc){
            worker_limit = atoi(argv[++i]);
            if(worker_limit <= 0){
                // if the worker limit gets an illegal value, fallback to the default value
                worker_limit = DEFAULT_WORKER_LIMIT;
            }
        } 
        else{
            perror("Unknown or incomplete argument provided on startup. Usage ./fss_manager -l <manager_logfile> -c <config_file> -n [optional]<worker_limit>");
            exit(EXIT_FAILURE);
        }
    }

    // check if any of the files wasn't provided correctly
    if(logfile == NULL){
        perror("Invalid logfile");
        exit(EXIT_FAILURE);
    }
    if(config_file == NULL){
        perror("Invalid config_file");
        exit(EXIT_FAILURE);
    }

    // opening the files
    FILE *cf, *lf;
    if((lf = fopen(logfile, "w")) == NULL){
        perror("opening the logfile");
        exit(1);
    }
    if((cf = fopen(config_file, "r")) == NULL){
        perror("opening the config file");
        exit(1);
    }

    // reading the config file line by line
    char line[MAX_LINE_LENGTH];

    // initialize the queue for work to be done
    Queue *work_queue = queue_create();

    // initializing the sync_info hashtable, with a default size of 101
    Hashtable *sync_storage = hashtable_with_default_size();

    // starting the inotify procedure
    int inotify_fd = inotify_init1(O_NONBLOCK);
    if(inotify_fd == -1){
        perror("inotify_init1");
        exit(1);
    }

    while(fgets(line, sizeof(line), cf) != NULL){
        worker_funeral(sync_storage, lf);
        char* source_dir = strtok(line, " ");
        char* target_dir = strtok (NULL, " ");
        // strip the \n from the end of target_dir
        target_dir[strcspn(target_dir, "\n")] = 0;
        // printf("Source = %s, Target = %s\n", source_dir, target_dir);
        SyncInfo* info = hashtable_get(sync_storage, source_dir);
        if(info != NULL && info->is_active == 1){
            // the source directory is already linked to a target directory, so we can skip it (do nothing)
            // printf("did nothing\n");
            continue;
        }
        info = malloc(sizeof(SyncInfo));
        info->source_dir = strdup(source_dir);
        info->target_dir = strdup(target_dir);
        char timestamp[64];
        get_timestamp(timestamp, sizeof(timestamp));
        info->last_sync_time = strdup(timestamp);
        info->is_active = 1;
        info->error_count = 0;
        // adding the source directory to the watch list, for created, modified or deleted files :DDDD 
        int wd = inotify_add_watch(inotify_fd, source_dir, IN_CREATE | IN_MODIFY | IN_DELETE); 
        if(wd == -1){
            perror("inotify_add_watch");
            free(info->source_dir);
            free(info->target_dir);
            if(info->last_sync_time) free(info->last_sync_time);
            free(info);
            // we skip this directory
            continue;
        }
        info->wd = wd;
        // insert the info struct to the hashtable
        sync_storage = hashtable_insert(sync_storage, info);

        // fork a worker to perform intial sync of the source directory to the target directory, ALL & FULL
        if(active_workers < worker_limit){
            if(!queue_is_empty(work_queue)){
                // if there are things to be done in the queue
                // we have to first enqueue the line that we just read, and give the worker the first available job from the queue
                enqueue(work_queue, source_dir, target_dir, "ALL", "FULL");
                char** dequeued_args = dequeue(work_queue);
                fork_worker(dequeued_args[0], dequeued_args[1], "ALL", "FULL", sync_storage, lf);
                for(int k=0; k<4; k++) free(dequeued_args[k]);
                free(dequeued_args);

            }
            else{
                fork_worker(source_dir, target_dir, "ALL", "FULL", sync_storage, lf);
                active_workers++;
            }
        }
        else{
            // append work to queue until a worker is free
            enqueue(work_queue, source_dir, target_dir, "ALL", "FULL");
        }
    }

    // function to empty the queue of sync items
    empty_work_queue(sync_storage, work_queue, worker_limit, lf);

    int fss_in = open_named_pipe(FSS_IN, O_RDONLY | O_NONBLOCK);

    // opening fss_out with O_RDWR instead of  O_WRONLY so as open doesnt return an error if the console still hasnt opened fss_out yet
    // but only write will be used in this pipe
    int fss_out = open_named_pipe(FSS_OUT, O_RDWR | O_NONBLOCK);

    // initialize the pollfd struct to not busy wait on the CPU forever
    struct pollfd poll_fd[2];
    poll_fd[0].fd = inotify_fd;
    poll_fd[0].events = POLLIN;
    poll_fd[1].fd = fss_in;
    poll_fd[1].events = POLLIN;

    int terminate = 0;
    while(!terminate){
        // the main working loop :D

        // we will start by always checking for dead workers
        worker_funeral(sync_storage, lf);

        // now we can also empty the queue (if there is anything in it)
        empty_work_queue(sync_storage, work_queue, worker_limit, lf);

        if(poll(poll_fd, 2, -1) > 0){
            // if there are inotify events, handle them
            if(poll_fd[0].revents & POLLIN){
                handle_inotify_events(inotify_fd, sync_storage, work_queue, worker_limit, lf);
            }
            // if there are console commands, handle them
            if(poll_fd[1].revents & POLLIN){
                handle_console_commands(fss_in, fss_out, sync_storage, work_queue, worker_limit, inotify_fd, &terminate, lf);
            }
        }
    }
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));
    char msg[MSGSIZE+1];
    snprintf(msg, sizeof(msg), "%s Shutting down manager...\n", timestamp);
    write_to_pipe(fss_out, msg);

    get_timestamp(timestamp, sizeof(timestamp));
    snprintf(msg, sizeof(msg), "%s Waiting for all active workers to finish.\n", timestamp);
    write_to_pipe(fss_out, msg);
    while(active_workers > 0){
        worker_funeral(sync_storage, lf);
    }

    get_timestamp(timestamp, sizeof(timestamp));
    snprintf(msg, sizeof(msg), "%s Processing remaining queued tasks.\n", timestamp);
    write_to_pipe(fss_out, msg);
    // empty the queue if we got shutdown with things still to be done
    empty_work_queue(sync_storage, work_queue, worker_limit, lf);

    // the final worker funeral for every hard worker who died - thank you for your service
    while(active_workers > 0){
        worker_funeral(sync_storage, lf);
    }
    queue_free(work_queue);
    hashtable_destroy(sync_storage);

    snprintf(msg, sizeof(msg), "Manager shutdown complete.");
    write_to_pipe(fss_out, msg);

    shutdown(fss_in, fss_out, cf, lf);
}

void shutdown(int fss_in, int fss_out, FILE *cf, FILE *lf){    
    close_pipe(fss_in);
    close_pipe(fss_out);
    remove_named_pipe(FSS_IN);
    remove_named_pipe(FSS_OUT);
    fclose(cf);
    fclose(lf);
}

void fork_worker(char *source_dir, char *target_dir, char *filename, char *operation, Hashtable* sync_storage, FILE *lf){
    // ./worker <source_dir> <target_dir> <filename> <operation>
    int p[2];
    if(pipe(p) == -1){
        perror("pipe call");
        exit(1);
    }
    
    pid_t pid = fork();

    if(pid == -1){
        perror("forking worker");
        exit(1);
    }
    else if(pid == 0){
        // the arguments to call execvp
        char *args[] = { "./worker", source_dir, target_dir, filename, operation, NULL};

        // VALGRIND ARGUMENTS FOR WORKERS
        // char *args[] = { "valgrind","./worker", source_dir, target_dir, filename, operation, NULL};

        // close the read end of the pipe, the child doesnt need it
        close(p[0]);
        // redirect the stdout of the child to the pipe (dup2)
        if(dup2(p[1], STDOUT_FILENO) == -1){
            perror("dup2");
            exit(1);
        }
        // we also close p[1] now, since we have duped stdout to the write end already :)
        close(p[1]);

        int e = execvp(args[0], args);
        if(e == -1){
            perror("execvp worker");
            exit(1);
        }
    }
    else{
        char timestamp[64];
        get_timestamp(timestamp, sizeof(timestamp));
        fprintf(lf, "%s Added directory: %s -> %s\n", timestamp, source_dir, target_dir);
        fprintf(lf, "%s Monitoring started for %s\n", timestamp, source_dir);
        fflush(lf);
        //Τυπώνει στην οθόνη και γράφει στο manager-log-file
        //[2025-02-10 10:00:01] Added directory: /home/user/docs -> /backup/docs
        //[2025-02-10 10:00:01] Monitoring started for /home/user/docs

        close(p[1]);
        // using our map utility to save the pipe with the pid, to consume the exec report as soon as the process dies normally
        add_worker_to_map(pid, p[0], source_dir, target_dir);
    }
}

void sighandler_worker(int signum){
    child_dead = 1;
}

void worker_funeral(Hashtable *sync_storage, FILE *lf){
    if (child_dead == 1){
        int exit_status;
        pid_t pid;
        
        while((pid = waitpid(-1, &exit_status, WNOHANG)) > 0 ){
            active_workers--;
            
            // reading the exec report using our map of pid -> pipe_fd
            char exec_report[1024] = {0};
            int pipe_fd = get_worker_pipe(pid);
            if(pipe_fd == -1){
                perror("invalid worker pid for mapping");
                exit(1);
            }
            int n = read(pipe_fd, exec_report, sizeof(exec_report) -1);
            if(n > 0){
                exec_report[n] = '\0';
                //append to manager logfile
                char* exec_report_copy = strdup(exec_report);
                char* status = NULL;
                if(strstr(exec_report, "STATUS: ") != NULL){
                    status = strstr(exec_report, "STATUS: ") + strlen("STATUS: ");
                    status = strtok(status, "\n");
                }
                char* details = NULL;
                if(strstr(exec_report_copy, "DETAILS: ") != NULL){
                    details = strstr(exec_report_copy, "DETAILS: ") + strlen("DETAILS: ");
                    details = strtok(details, "\n");
                }
                char timestamp[64];
                get_timestamp(timestamp, sizeof(timestamp));
                fprintf(lf, "%s [%s] [%s] [%d] [%s] [%s]\n", timestamp, get_worker_source_dir(pid), get_worker_target_dir(pid), pid, status, details);
                fflush(lf);
                free(exec_report_copy);
            }
            // retreive the source_directory that the worker was working on 
            char* source_dir = get_worker_source_dir(pid);
            if(source_dir != NULL){
                SyncInfo *info = hashtable_get(sync_storage, source_dir);
                if(info != NULL){
                    char timestamp[64];
                    get_timestamp(timestamp, sizeof(timestamp));
                    // if(info->last_sync_time) free(info->last_sync_time);
                    info->last_sync_time = strdup(timestamp);
                    info->error_count = get_error_count(exec_report);
                }
            }
            // we can now close the other end of the pipe
            close(pipe_fd);
            remove_worker_from_map(pid);
        }
        child_dead = 0;
    }
}

void get_timestamp(char* buffer, size_t size) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    strftime(buffer, size, "[%Y-%m-%d %H:%M:%S]", tm_info);
}

void handle_inotify_events(int inotify_fd, Hashtable *sync_storage, Queue* work_queue, int worker_limit, FILE *lf){
    char buffer[INOTIFY_BUF_LEN];
    int length = read(inotify_fd, buffer, INOTIFY_BUF_LEN);

    // since our read is non blocking, we have to first see if there was any actual change made
    if( length < 0){
        if (errno == EAGAIN || errno == EWOULDBLOCK){
            // read didn't actually read any event, not because of an error but because the call is non blocking
            // we can safely return to our fss_console while(1)
            return;
        }
        else{
            perror("inotify read");
            exit(1);
        }
    }

    // here we handle all 3 inotify possible cases we are checking for
    int i = 0;
    while(i < length){
        struct inotify_event *event = (struct inotify_event *) &buffer[i];
        // we have to load our SyncInfo struct where the event->wd is connected to from the hashtable 
        if(event->len > 0){
            SyncInfo *info = hashtable_get_by_wd(sync_storage, event->wd);
            char* operation = NULL;
            if(info == NULL){
                perror("This wd doesnt correspond to a monitored catalogue");
                return;
            }
            else if(event->mask & IN_CREATE){
                operation = "ADDED";
                printf("went in add\n");
            }
            else if(event->mask & IN_DELETE){
                operation = "DELETED";
                printf("went in delete\n");
            }
            else if(event->mask & IN_MODIFY){
                //tried with IN_MODIFY but there was a bug where 2 workers were spawned
                operation = "MODIFIED";
                printf("went in modify\n");
            }
            else{
                // perror("Unknown operation done to the source directory - cannot sync it");
                return;
            }
            if(operation != NULL){
                // now that we have all available information, we can fork a worker to sync the directory, 
                // with the changes made to event->name (filename) and the opration that we encoded above
                if(active_workers < worker_limit && queue_is_empty(work_queue)){
                    // if the queue is empty and i can spawn a worker, directly deal with the current operation 
                    fork_worker(info->source_dir, info->target_dir, event->name, operation, sync_storage, lf);
                    active_workers++;
                }
                else{
                    // append work to queue until a worker is free
                    enqueue(work_queue, info->source_dir, info->target_dir, event->name, operation);
                }
            }
            else{
                perror("Unknown error when handling operation");
                return;
            }
        }
        else{
            perror("event->len");
            return;
        }
        i += INOTIFY_EVENT_SIZE + event->len;
    } 

}

void handle_console_commands(int fss_in, int fss_out, Hashtable* sync_storage, Queue* work_queue, int worker_limit, int inotify_fd, int *terminate, FILE *lf){
    char buffer[512];
    int length = read(fss_in, buffer, sizeof(buffer) - 1);
    // since our read is non blocking, we have to first see if there was any command given
    if( length < 0){
        if (errno == EAGAIN || errno == EWOULDBLOCK){
            // read didn't actually read any event, not because of an error but because the call is non blocking
            // we can safely return to our fss_console while(1)
            return;
        }
        else{
            perror("fss_in reading");
            exit(1);
        }
    }

    buffer[length] = '\0';

    // the most important case - if we get shutdown, we exit the program
    if(strcmp(buffer, "shutdown\n") == 0){
        (*terminate) = 1;
        return;
    }

    // breakdown of the command
    char* command = strtok(buffer, " \n");
    if(command == NULL){
        char timestamp[64];
        get_timestamp(timestamp, sizeof(timestamp));
        char msg[MSGSIZE+1];
        snprintf(msg, sizeof(msg), "%s Error: Unknown Command provided.\n", timestamp);
        write_to_pipe(fss_out, msg);
        return;
    }
    
    if(strcmp(command, "add") == 0){
        char* source_dir = strtok(NULL, " \n");
        char* target_dir = strtok(NULL, " \n");
        if(source_dir == NULL || target_dir == NULL){
            char timestamp[64];
            get_timestamp(timestamp, sizeof(timestamp));
            char msg[MSGSIZE+1];
            snprintf(msg, sizeof(msg), "%s Error: Invalid add command. Correct usage: add <source> <target>\n", timestamp);
            write_to_pipe(fss_out, msg);
            return;
        }

        SyncInfo* info = hashtable_get(sync_storage, source_dir);
        char timestamp[64];
        if(info != NULL ){
            if(info->is_active == 1){
                // the source directory is already being linked to a target directory, so we can skip it (do nothing)
                get_timestamp(timestamp, sizeof(timestamp));
                char msg[MSGSIZE+1];
                snprintf(msg, sizeof(msg), "%s Already monitored: %s\n", timestamp, source_dir);
                write_to_pipe(fss_out, msg);
                return;
            }
            else{
                info->target_dir = strdup(target_dir);
                char timestamp[64];
                get_timestamp(timestamp, sizeof(timestamp));
                info->last_sync_time = strdup(timestamp);
                info->is_active = 1;
                int wd = inotify_add_watch(inotify_fd, source_dir, IN_CREATE | IN_MODIFY | IN_DELETE); 
                if(wd == -1){
                    get_timestamp(timestamp, sizeof(timestamp));
                    char msg[MSGSIZE+1];
                    snprintf(msg, sizeof(msg), "%s Error: inotify_add_watch when adding new directory.\n", timestamp);
                    write_to_pipe(fss_out, msg);
                    free(info);
                    // we skip this directory
                    return;
                }
                info->wd = wd;
            }
        }
        else{
            info = malloc(sizeof(SyncInfo));
            info->source_dir = strdup(source_dir);
            info->target_dir = strdup(target_dir);
            get_timestamp(timestamp, sizeof(timestamp));
            info->last_sync_time = strdup(timestamp);
            info->is_active = 1;
            // adding the source directory to the watch list, for created, modified or deleted files :DDDD 
            int wd = inotify_add_watch(inotify_fd, source_dir, IN_CREATE | IN_MODIFY | IN_DELETE); 
            if(wd == -1){
                get_timestamp(timestamp, sizeof(timestamp));
                char msg[MSGSIZE+1];
                snprintf(msg, sizeof(msg), "%s Error: inotify_add_watch when adding new directory.\n", timestamp);
                write_to_pipe(fss_out, msg);
                free(info);
                // we skip this directory
                return;
            }
            info->wd = wd;
            // insert the info struct to the hashtable
            sync_storage = hashtable_insert(sync_storage, info);
        }
        // fork a worker to perform intial sync of the source directory to the target directory, ALL & FULL
        if(active_workers < worker_limit && queue_is_empty(work_queue)){
            fork_worker(source_dir, target_dir, "ALL", "FULL", sync_storage, lf);
            active_workers++;
        }
        else{
            // append work to queue until a worker is free
            enqueue(work_queue, source_dir, target_dir, "ALL", "FULL");
        }
        char msg[MSGSIZE+1];
        snprintf(msg, sizeof(msg), "%s Added directory %s -> %s\n%s Monitoring started for %s successfully.\n", 
                        timestamp, source_dir, target_dir, timestamp, source_dir);
        write_to_pipe(fss_out, msg);
    }
    else if(strcmp(command, "status") == 0){
        char* source_dir = strtok(NULL, " \n");
        if(source_dir == NULL){
            char timestamp[64];
            get_timestamp(timestamp, sizeof(timestamp));
            char msg[MSGSIZE+1];
            snprintf(msg, sizeof(msg), "%s Error: Invalid status command. Correct usage: status <directory>\n", timestamp);
            write_to_pipe(fss_out, msg);
            return;
        }
        SyncInfo* info = hashtable_get(sync_storage, source_dir);
        if(info == NULL){
            char timestamp[64];
            get_timestamp(timestamp, sizeof(timestamp));
            char msg[MSGSIZE+1];
            snprintf(msg, sizeof(msg), "%s Directory not monitored %s\n", timestamp, source_dir);
            write_to_pipe(fss_out, msg);
            return;
        }
        char timestamp[64];
        get_timestamp(timestamp, sizeof(timestamp));
        char msg[MSGSIZE+1];
        char status[64];
        if (info->is_active == 1){
            strcpy(status, "Active");
        }
        else{
            strcpy(status, "Inactive");
        }
        snprintf(msg, sizeof(msg), "%s Status requested for %s\n Directory: %s\n Target: %s\n Last Sync: %s\n Errors: %d\n Status: %s\n", 
                            timestamp, info->source_dir, info->source_dir, info->target_dir, info->last_sync_time, info->error_count, status);
        write_to_pipe(fss_out, msg);
    }
    else if(strcmp(command, "cancel") == 0){
        char* source_dir = strtok(NULL, " \n");
        if(source_dir == NULL){
            char timestamp[64];
            get_timestamp(timestamp, sizeof(timestamp));
            char msg[MSGSIZE+1];
            snprintf(msg, sizeof(msg), "%s Error: Invalid cancel command. Correct usage: cancel <directory>\n", timestamp);
            write_to_pipe(fss_out, msg);
            return;
        }
        SyncInfo* info = hashtable_get(sync_storage, source_dir);
        if(info != NULL){
            if(info->is_active == 0){
                char timestamp[64];
                get_timestamp(timestamp, sizeof(timestamp));
                char msg[MSGSIZE+1];
                snprintf(msg, sizeof(msg), "%s Directory not monitored %s\n", timestamp, source_dir);
                write_to_pipe(fss_out, msg);
                return;
            }
            inotify_rm_watch(inotify_fd, info->wd);
            info->is_active = 0;

            char timestamp[64];
            get_timestamp(timestamp, sizeof(timestamp));
            char msg[MSGSIZE+1];
            snprintf(msg, sizeof(msg), "%s Monitoring stopped for %s\n", timestamp, source_dir);
            write_to_pipe(fss_out, msg);
            // log to manager logfile
            fprintf(lf, "%s Monitoring stopped for %s\n", timestamp, source_dir);
            fflush(lf);
        }
        else{
            char timestamp[64];
            get_timestamp(timestamp, sizeof(timestamp));
            char msg[MSGSIZE+1];
            snprintf(msg, sizeof(msg), "%s Error: Directory not found\n", timestamp);
            write_to_pipe(fss_out, msg);
            return;
        }
    }
    else if(strcmp(command, "sync") == 0){
        char* source_dir = strtok(NULL, " \n");
        if(source_dir == NULL){
            char timestamp[64];
            get_timestamp(timestamp, sizeof(timestamp));
            char msg[MSGSIZE+1];
            snprintf(msg, sizeof(msg), "%s Error: Invalid sync command. Correct usage: sync <directory>\n", timestamp);
            write_to_pipe(fss_out, msg);
            return;
        }
        SyncInfo* info = hashtable_get(sync_storage, source_dir);
        if(info == NULL){
            char timestamp[64];
            get_timestamp(timestamp, sizeof(timestamp));
            char msg[MSGSIZE+1];
            snprintf(msg, sizeof(msg), "%s Error: This directory hasn't ever been monitored.\n", timestamp);
            write_to_pipe(fss_out, msg);
            return;
        }
        if(info->is_active == 0){
            // set the directory as active
            info->is_active = 1;
        }
        if(active_workers < worker_limit && queue_is_empty(work_queue)){
            fork_worker(source_dir, info->target_dir, "ALL", "FULL", sync_storage, lf);
            active_workers++;
        }
        else{
            // append work to queue until a worker is free
            enqueue(work_queue, source_dir, info->target_dir, "ALL", "FULL");
        }
        char timestamp[64];
        get_timestamp(timestamp, sizeof(timestamp));
        char msg[MSGSIZE+1];
        snprintf(msg, sizeof(msg), "%s Syncing directory: %s -> %s\n", timestamp, source_dir, info->target_dir);
        write_to_pipe(fss_out, msg);
        fprintf(lf, "%s Syncing directory: %s -> %s\n", timestamp, source_dir, info->target_dir);
        get_timestamp(timestamp, sizeof(timestamp));
        snprintf(msg, sizeof(msg), "%s Sync completed: %s -> %s Errors: %d\n", timestamp, source_dir, info->target_dir, info->error_count);
        write_to_pipe(fss_out, msg);
        fprintf(lf, "%s Sync completed: %s -> %s Errors: %d\n", timestamp, source_dir, info->target_dir, info->error_count);
        fflush(lf);
    }
    else{
        char timestamp[64];
        get_timestamp(timestamp, sizeof(timestamp));
        char msg[MSGSIZE+1];
        snprintf(msg, sizeof(msg), "%s Error: Unknown Command provided.\n", timestamp);
        write_to_pipe(fss_out, msg);
    }
}

void empty_work_queue(Hashtable* sync_storage, Queue* work_queue, int worker_limit, FILE *lf){
    while(!queue_is_empty(work_queue)){
        worker_funeral(sync_storage, lf);
        // finish reading and initalizing from the queue
        if(active_workers <= worker_limit){
            char** dequeued_args = dequeue(work_queue);
            fork_worker(dequeued_args[0], dequeued_args[1], "ALL", "FULL", sync_storage, lf);
            active_workers++;

            for(int k=0; k<4; k++) free(dequeued_args[k]);
            free(dequeued_args);
        }
    }
}

int get_error_count(char* report){
    char* errors_section = strstr(report, "ERRORS:");
    if(errors_section == NULL){
        return 0;
    }
    int count = 0;
    char* p = errors_section;
    while((p = strchr(p, '-')) != NULL){
        count++;
        // go after the -, to then continue on the new line in the while
        p++; 
    }
    return count;
}