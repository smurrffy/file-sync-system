CC=gcc

CFLAGS = -g3 -Wall -Werror

MANAGER = fss_manager
CONSOLE = fss_console
WORKER = worker

MANAGER_OBJS = fss_manager.o pipes.o queue.o list.o utilities.o sync_info_mem_store.o
CONSOLE_OBJS = fss_console.o pipes.o queue.o list.o utilities.o sync_info_mem_store.o
WORKER_OBJS = worker.o

all: $(MANAGER) $(CONSOLE) $(WORKER)

$(MANAGER): $(MANAGER_OBJS)
	$(CC) $(CFLAGS) $(MANAGER_OBJS) -o $(MANAGER)

$(CONSOLE): $(CONSOLE_OBJS)
	$(CC) $(CFLAGS) $(CONSOLE_OBJS) -o $(CONSOLE)
	
$(WORKER): $(WORKER_OBJS)
	$(CC) $(CFLAGS) $(WORKER_OBJS) -o $(WORKER)


clean:
	rm -f $(MANAGER) $(MANAGER_OBJS) $(CONSOLE) $(CONSOLE_OBJS) $(WORKER) $(WORKER_OBJS)
	rm -f fss_in fss_out
	rm -rf tests/folder2 tests/folder4 tests/folder6 SKIP_BECAUSE_OF_DUPLICATE