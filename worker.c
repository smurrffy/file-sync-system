#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#define BUFFSIZE 1024

int copy_file(char* src_file_path, char* trg_file_path, char** errors, int *error_count){
    // opening the source file, if we get an error we are appending it to the errors buffer and returning
    int source_fd = open(src_file_path, O_RDONLY);
    if(source_fd < 0){
        errors[*error_count] = malloc(512);
        if(errors[*error_count] != NULL){
            snprintf(errors[*error_count], 512, "File %s: %s\n", src_file_path, strerror(errno));
            (*error_count)++;
            return -1;
        }
        else{
            //malloc error
            exit(1);
        }
    }
    // open, create or overwrite the target file, while checking and appending errors
    int target_fd = open(trg_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(target_fd < 0){
        if(errors[*error_count] != NULL){
            snprintf(errors[*error_count], 512, "File %s: %s\n", src_file_path, strerror(errno));
            (*error_count)++;
            // also close the source fd now
            close(source_fd);
            return -1;
        }
        else{
            //malloc error
            exit(1);
        }
    }
    // once we reach here, we have both the source and target files open and ready to copy
    char buffer[BUFFSIZE];
    int n;
    while((n = read(source_fd, buffer, sizeof(buffer))) > 0){
        write(target_fd, buffer, n);
    }
    // successful copying of all bytes of the surce to the target, close file dsecriptors and return success
    close(target_fd);
    close(source_fd);
    return 0;
}

int full_sync(char* source_dir, char* target_dir, char** errors, int *error_count, int *copied, int *skipped){
    DIR *source = opendir(source_dir);
    if(!source){
        // if there is an error opening the directory, we append it to our errors array for the exec report
        // and exit the function, there is nothing else we can do
        errors[*error_count] = malloc(512);
        if (errors[*error_count] != NULL) {
            snprintf(errors[*error_count], 512, "Directory %s: %s\n", source_dir, strerror(errno));
        }
        else{
            //malloc error
            exit(1);
        }
    }

    struct stat st;
    if(stat(target_dir, &st) == -1){
        if(mkdir(target_dir, 0755) == -1){
            errors[*error_count] = malloc(512);
            if (errors[*error_count] != NULL) {
                snprintf(errors[*error_count], 512, "Directory %s: %s\n", source_dir, strerror(errno));
                (*error_count)++;
                return -1;
            }
            else{
                //malloc error
                exit(1);
            }
        }
    }

    // we will use this struct to iterate through all entries of a directory
    struct dirent *entry;
    while((entry = readdir(source)) != NULL){
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0){
            // we skip the subdirectories . and .. , since we can't do anything with them
            continue;
        }

        // we have to change the filename of the entry to source_path/filename and target_path/filename, 
        // to be able to copy the file to the correct place
        char source_file_path[512];
        snprintf(source_file_path, sizeof(source_file_path), "%s/%s", source_dir, entry->d_name);
        char target_file_path[512];
        snprintf(target_file_path, sizeof(target_file_path), "%s/%s", target_dir, entry->d_name);

        // call the copy file func with the new file paths and append the result to our counters
        if(copy_file(source_file_path, target_file_path, errors, error_count) == 0){
            (*copied)++;
        }
        else{
            (*skipped)++;
        }
    }
    // done successfully with all the items in the directory, and we have kept the logs in the counters
    closedir(source);
    return 0;
}

void exec_report(char* status, char* details, char** errors, int error_count){
    printf("EXEC_REPORT_START\n");
    printf("STATUS: %s\n", status);
    printf("DETAILS: %s\n", details);
    if(error_count > 0){
        printf("ERRORS:\n");
        for(int i = 0; i < error_count; i++){
            printf("-\t%s", errors[i]);
        }
    }
    printf("EXEC_REPORT_END\n");
}

int main(int argc, char **argv){
    // printf("Worker done with source: %s, target: %s, filename: %s, operation %s\n", argv[1], argv[2], argv[3], argv[4]);
    char* source_dir = argv[1];
    char* target_dir = argv[2];
    char* filename = argv[3];
    char* operation = argv[4];

    int error_count = 0;
    char* errors[128];
    char details[128];
    
    if(strcmp(filename, "ALL") == 0 && strcmp(operation, "ALL")){
        // full/initial sync option
        int copied = 0;
        int skipped = 0;
        // call the full sync function with all the above arguments as needed
        full_sync(source_dir, target_dir, errors, &error_count, &copied, &skipped);
        if(error_count == 0){
            snprintf(details, sizeof(details), "%d files copied", copied);
            exec_report("SUCCESS", details, errors, error_count);
        }
        else{
            snprintf(details, sizeof(details), "%d files copied, %d files skipped", copied, skipped);
            if(copied > 0){
                exec_report("PARTIAL", details, errors, error_count);
            }
            else{
                exec_report("ERROR", details, errors, error_count);
            }
        }
    }
    else if(strcmp(operation, "ADDED") == 0 || strcmp(operation, "MODIFIED") == 0){
        // added and modified have the same handling, we just overwrite the old file with the new one

        // get the full paths of the file added and the target path of where to add it
        char source_file_path[512];
        snprintf(source_file_path, sizeof(source_file_path), "%s/%s", source_dir, filename);
        char target_file_path[512];
        snprintf(target_file_path, sizeof(target_file_path), "%s/%s", target_dir, filename);

        if(copy_file(source_file_path, target_file_path, errors, &error_count) == 0){
            snprintf(details, sizeof(details), "File: %s", filename);
            exec_report("SUCCESS", details, errors, error_count);
        }
        else{
            snprintf(details, sizeof(details), "File: %s", filename);
            exec_report("ERROR", details, errors, error_count);
        }
    }
    else if(strcmp(operation, "DELETED") == 0){
        perror("goes in");
        // if we have to handle a deletion, we only need the correct target path
        char target_file_path[512];
        snprintf(target_file_path, sizeof(target_file_path), "%s/%s", target_dir, filename);
        // we only have to use the unlink function for this
        if(unlink(target_file_path) == 0){
            snprintf(details, sizeof(details), "File: %s", filename);
            exec_report("SUCCESS", details, errors, error_count);
        }
        else{
            // since here we are using unlink, and not our own copy/sync function, the errors will have to appended here
            errors[error_count] = malloc(512);
            if (errors[error_count] != NULL) {
                snprintf(errors[error_count], 512, "File %s: %s\n", filename, strerror(errno));
            }            
            error_count++;
            snprintf(details, sizeof(details), "File: %s", filename);
            exec_report("ERROR", details, errors, error_count); 
        }
    }
    else{
        snprintf(details, sizeof(details), "Unknown operation requested: %s", operation);
        exec_report("ERROR", details, NULL, 0);
    }

    // free all memory malloc'd, since we have already sent everything through the pipe
    for (int i = 0; i < error_count; i++) {
        free(errors[i]);
    }
}