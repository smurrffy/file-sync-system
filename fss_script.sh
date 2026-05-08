#!/bin/bash

if [ "$#" -ne 4 ]; then
    echo "Incorrect usage. Please use ./fss_script.sh -p <path> -c <command>"
    exit 1
fi 

if [ "$1" != "-p" ]; then
    echo "Incorrect usage. Please use ./fss_script.sh -p <path> -c <command>"
    exit 1
fi 

if [ "$3" != "-c" ]; then
    echo "Incorrect usage. Please use ./fss_script.sh -p <path> -c <command>"
    exit 1
fi 

path=$2
command=$4

if [ "$command" == "purge" ]; then
    # do purge and exit
    if [ ! -e "$path" ]; then
        echo "Error: The specified path does not exit"
        exit 1
    fi

    if [ -f "$path" ]; then
        rm "$path"
        echo "Deleting $path..."
    elif [ -d "$path" ]; then
        rm -rf "$path"
        echo "Deleting $path..."
    else
        echo "The path is neither a logfile nor a directory"
        exit 1
    fi

    echo "Purge complete."
    exit 0
fi

# we declare two array variables that we will keep as a mapping of the logfile
declare -A target_map
declare -A time_map
declare -A active_map
declare -A status_map


if [ ! -f "$path" ]; then
    echo "Error: This logfile does not exist."
    exit 1
fi
# map the occurences of a source directory with the target directory, the last sync time and the stopped or not
while read -r line; do
    bracket_count=$(echo "$line" | grep -o "\[" | wc -l)

    if [ "$bracket_count" -eq 6 ]; then
        # if we have a line with 6 open brackets, aka a [][][][][][] line

        # the first element is the timestamp
        timestamp=$(echo "$line" | cut -d']' -f1 | tr -d '[]')
        # the second elements is the source
        source_dir=$(echo "$line" | cut -d']' -f2 | tr -d ' []')
        # the third element is the target
        target_dir=$(echo "$line" | cut -d']' -f3 | tr -d ' []')
        # the fifth element is the status, we skip the fourth because it's the pid
        status=$(echo "$line" | cut -d']' -f5 | tr -d ' []')
        
        if [ -z "${time_map["$source_dir"]}" ]; then
            # if this is the first occurence of the source_dir in the map(s), we just add it
            time_map["$source_dir"]="$timestamp"
            target_map["$source_dir"]="$target_dir"
            status_map["$source_dir"]="$status"
        else
            # if there is a more recent change to the time mapping of our source directory, we keep the most recent one
            if [ "$timestamp" \> "$time_map["$source_dir"]" ]; then
                time_map["$source_dir"]="$timestamp"
                
                # if there are changes to our source -> target associations as we go down the file,
                # we append the new ones to the map
                if [ "$target_dir" != "$target_map["$source_dir"]" ]; then
                    target_map["$source_dir"]="$target_dir"
                fi

                #if the status (success, error, partial) changed, keep the most recent one
                if [ "$status" != "$status_map["$source_dir"]" ]; then
                    status_map["$source_dir"]="$status"
                fi
            fi
        fi
    fi

    if [[ "$line" =~ Monitoring\ stopped\ for\ (.*) ]]; then
        # getting the source directory as the regex match
        source_dir="${BASH_REMATCH[1]}"
        active_map["$source_dir"]="STOPPED"
    fi

    if [[ "$line" =~ Monitoring\ started\ for\ (.*) ]]; then
        source_dir="${BASH_REMATCH[1]}"
        active_map["$source_dir"]="ACTIVE"
    fi
    
done < "$path"

counter=0

# now for every command all we have to do is retrieve the information from the maps and print

if [ "$command" == "listAll" ]; then
    for source_dir in "${!target_map[@]}"; do
        target_dir=${target_map[$source_dir]}
        timestamp=${time_map[$source_dir]}
        status=${status_map[$source_dir]}
        echo "$source_dir -> $target_dir [Last Sync: $timestamp] [$status]"
    done
elif [ "$command" == "listMonitored" ]; then
    for source_dir in "${!target_map[@]}"; do

        if [ "${active_map["$source_dir"]}" == "ACTIVE" ]; then
            target_dir=${target_map[$source_dir]}
            timestamp=${time_map[$source_dir]}
            echo "$source_dir -> $target_dir [Last Sync: $timestamp]"
            counter+=1
        fi
    
    done
    if [ "$counter" -eq 0 ]; then
        echo "No directory is being monitored"
    fi
elif [ "$command" == "listStopped" ]; then
    for source_dir in "${!target_map[@]}"; do
    
        if [ "${active_map["$source_dir"]}" == "STOPPED" ]; then
            target_dir=${target_map[$source_dir]}
            timestamp=${time_map[$source_dir]}
            echo "$source_dir -> $target_dir [Last Sync: $timestamp]"
            counter+=1
        fi
    
    done
    if [ "$counter" -eq 0 ]; then
        echo "No directory has been stopped"
    fi
else 
    echo "Error: Unknown command."
    exit 1
fi 
exit 0

