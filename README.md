# FSS — File Synchronization System

A multi-process file synchronization daemon built in C for Linux. `fss_manager` watches source directories using `inotify`, spawning lightweight worker processes to mirror changes to target directories in near real-time. A companion CLI (`fss_console`) communicates with the manager over named pipes, and a Bash utility script (`fss_script.sh`) provides status inspection and cleanup commands.

## Tech Stack

| Layer | Technology |
|---|---|
| Language | C (C11), Bash |
| IPC | Named pipes (`mkfifo`), anonymous pipes |
| Directory monitoring | Linux `inotify` |
| Concurrency | `fork`/`exec` worker processes, `poll`-based event loop, POSIX signals |
| Data structures | Custom hashtable, queue, and linked list (no external libs) |
| Build | GNU Make, GCC |

## Architecture

```
fss_console  ──(fss_in/fss_out named pipes)──►  fss_manager
                                                      │
                                          ┌───────────┼───────────┐
                                       worker       worker      worker
                                    (sync job)   (sync job)  (queued…)
```

- **`fss_manager`** — daemon: parses a config file of `<source> <target>` pairs, sets up inotify watches, manages a pool of worker processes (configurable limit, default 5), and handles console commands.
- **`fss_console`** — thin CLI: forwards user commands to the manager and prints responses. Must be started after the manager.
- **`worker`** — forked for each sync job; copies changed files and reports back via pipe.
- **`fss_script.sh`** — reads the manager log to list monitored/stopped directories and purge targets.

## Build

```bash
make          # builds fss_manager, fss_console, and worker
make clean    # removes binaries, object files, and named pipes
```

Requires GCC and Linux kernel ≥ 2.6.36 (inotify support).

## Usage

**1. Start the manager** (must come first):

```bash
./fss_manager -l manager_logfile.txt -c config_file.txt [-n <worker_limit>]
```

**2. Open the console** in a separate terminal:

```bash
./fss_console -l console_logfile.txt
```

**3. Send commands via the console:**

| Command | Description |
|---|---|
| `add <source> <target>` | Begin monitoring `source`, syncing changes to `target` |
| `status <directory>` | Show sync status for a directory |
| `sync <directory>` | Trigger an immediate sync |
| `cancel <source>` | Stop monitoring a directory |
| `shutdown` | Gracefully shut down the manager (waits for active workers) |

**4. Query state with the script:**

```bash
./fss_script.sh -p <manager_logfile> -c listAll        # all sync pairs + timestamps
./fss_script.sh -p <manager_logfile> -c listMonitored  # actively watched directories
./fss_script.sh -p <manager_logfile> -c listStopped    # cancelled directories
./fss_script.sh -p <path>           -c purge           # delete a target dir or log file
```

**Config file format** (`config_file.txt`):

```
/path/to/source1 /path/to/target1
/path/to/source2 /path/to/target2
```

## Running the Included Tests

The `tests/` directory contains three pre-made source directories with sample files:

| Source | Contents | Target (auto-created) |
|---|---|---|
| `tests/folder1` | `test.c`, `lalala`, `new` | `tests/folder2` |
| `tests/folder3` | `test.c`, `vader_lexicon.zip` | `tests/folder4` |
| `tests/folder5` | PDF document | `tests/folder6` |

The provided `config_file.txt` points to these directories using relative paths, so you can run the system from the repo root with no changes:

```bash
./fss_manager -l manager_logfile.txt -c config_file.txt
```

The config also includes a duplicate-source entry (`tests/folder1` → `SKIP_BECAUSE_OF_DUPLICATE`) to test that the manager correctly rejects duplicate source registrations.

Target directories are created automatically by the manager on first sync. `make clean` removes them.

### Adding your own test case

1. Create a source directory with some files:
   ```bash
   mkdir tests/my-source
   echo "hello" > tests/my-source/sample.txt
   ```

2. Add a line to `config_file.txt`:
   ```
   tests/my-source tests/my-target
   ```

3. Start the manager and console as usual. Any file created, modified, or deleted in `tests/my-source` will be mirrored to `tests/my-target` automatically.

## Notes

- Both absolute and relative paths are supported, but do not mix them for the same pair.
- If the worker limit (`-n`) is omitted or invalid, it defaults to 5.
- The manager uses `poll` to multiplex inotify events and console commands without busy-waiting.
- All sync state is stored in a hash table keyed by source path for O(1) lookups.
