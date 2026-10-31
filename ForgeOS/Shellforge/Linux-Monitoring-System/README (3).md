# ForgeShell – Linux Process Monitoring and Control System

ForgeShell is a menu-driven, command-line application written in **C (C11)** for **Ubuntu Linux**. It provides a single interface to list, search, inspect, monitor, and control Linux processes.

> **Project:** Operating Systems and Systems Programming  
> **Team:** Team 01 – Kernel Crafters  
> **Team Members:**  
> - Alekhya — `2520030209`
> - Devika — `2520030329`
> - Tanmayee — `2520030339`

## Abstract

Linux systems run many processes simultaneously. Finding a particular process, understanding its state, checking its resource information, and controlling its execution can require several separate commands. ForgeShell brings these tasks together in one terminal-based application.

The program reads process and system information from the Linux `/proc` virtual filesystem. It supports process listing and searching, detailed process inspection, periodic monitoring, and process-control operations using the `kill()` system call and POSIX signals. The monitoring view refreshes every two seconds and estimates CPU usage from consecutive process and system CPU-time samples. Input validation, error reporting, confirmation prompts, and safeguards for PID 1 and ForgeShell itself are included.

## Features

1. **List Running Processes** – displays PID, PPID, process state, and process name.
2. **Search Process** – searches by an exact numeric PID or by a case-insensitive process-name substring.
3. **Inspect Process** – displays process name, PID, PPID, state, VmSize, VmRSS, UID, thread count, and accumulated CPU time when available.
4. **Control Process** – offers:
   - `SIGTERM` – request graceful termination.
   - `SIGKILL` – force termination.
   - `SIGSTOP` – stop/pause a process.
   - `SIGCONT` – continue a stopped process.
5. **Monitor Process** – displays process state, resident memory, and estimated CPU usage; refreshes every two seconds. Press `Q` or `Ctrl+C` to stop monitoring.

## Technology Stack

| Component | Technology |
|---|---|
| Programming language | C (C11) |
| Operating system | Ubuntu Linux 24.04 |
| Compiler | GCC |
| Process information | Linux `/proc` virtual filesystem |
| Process control | `kill()` and POSIX signals |
| Terminal interaction | Standard C input/output, `select()`, and `termios` |

## Repository Structure

```text
ForgeShell/
├── main.c
├── README.md
└── docs/
    └── screenshots/
        ├── 01-main-menu-and-exit.png
        ├── 02-list-running-processes.png
        ├── 03-search-process-by-pid.png
        ├── 04-inspect-process-details.png
        └── 05-monitor-invalid-pid-handling.png
```

## Setup and Execution

### 1. Open the project directory

```bash
cd ForgeShell
```

### 2. Compile

```bash
gcc -Wall -Wextra -Wpedantic -std=c11 main.c -o shellforge
```

### 3. Run

```bash
./shellforge
```

The main menu provides these options:

```text
1. List Running Processes
2. Search Process
3. Inspect Process
4. Control Process
5. Monitor Process
6. Exit
```

## Screenshots and Observed Output

The following screenshots are from the project's terminal execution.

### 1. Main Menu and Exit

The application displays the ForgeShell menu and exits when option `6` is selected.

![ForgeShell main menu and exit](docs/screenshots/01-main-menu-and-exit.png)

### 2. List Running Processes

Selecting option `1` displayed a process table containing PID, PPID, state, and name. **The captured run listed 28 processes.**

![List running processes](docs/screenshots/02-list-running-processes.png)

### 3. Search Process by PID

Selecting option `2` and entering PID `210` returned one matching process: `rsyslogd`.

```text
Enter PID, process name or part of a name: 210

PID      PPID     STATE          NAME
210      1        S (Sleeping)   rsyslogd

1 match found.
```

![Search process by PID](docs/screenshots/03-search-process-by-pid.png)

### 4. Inspect Process Details

Selecting option `3` and entering PID `114` displayed information for the `snapfuse` process in the captured run.

```text
Process name          : snapfuse
PID                   : 114
Parent PID (PPID)     : 1
State                 : S (Sleeping)
Virtual mem (VmSize)  : 153080 kB (149.49 MB)
Resident mem (VmRSS)  : 1856 kB (1.81 MB)
User ID               : 0 (root)
Threads               : 3
CPU time (total)      : 0.00 s (user 0.00 s, system 0.00 s)
```

These are the values shown in that particular execution; process information can change between runs.

![Inspect process details](docs/screenshots/04-inspect-process-details.png)

### 5. Invalid PID Handling in Monitor Option

Selecting option `5` and entering PID `3925` produced the following message:

```text
Enter PID: 3925
Error: process 3925 does not exist (it may have just exited).
```

This demonstrates that the application reports a process that cannot be found instead of attempting to monitor it.

**Note:** This screenshot demonstrates invalid-PID handling. It is not evidence of a successful live-monitoring session.

![Monitor invalid PID handling](docs/screenshots/05-monitor-invalid-pid-handling.png)

## Test Cases and Results

The table below records only the behavior visible in the supplied screenshots. It does not claim that unshown operations were tested.

| Test ID | Feature | Test input / action | Observed result | Status |
|---|---|---|---|---|
| TC01 | Main menu and exit | Launch `./shellforge`; select `6` | Main menu appeared and the application displayed its exit message | Pass |
| TC02 | List processes | Select `1` | Process table displayed PID, PPID, state, and name; 28 processes were listed in the captured run | Pass |
| TC03 | Search by PID | Select `2`; enter `210` | One matching process, `rsyslogd`, was displayed | Pass |
| TC04 | Inspect process | Select `3`; enter `114` | Details for `snapfuse` were displayed, including memory, UID, threads, and CPU time | Pass |
| TC05 | Monitor invalid PID | Select `5`; enter `3925` | Application reported that the process did not exist | Pass – error handling |

### Additional tests to perform

The screenshots supplied so far do **not** show successful execution of the following tests. Run them before marking them as passed in a final test report:

| Test ID | Feature | Suggested test procedure | Expected behavior |
|---|---|---|---|
| TC06 | Search by process name | Search for a known process name such as `bash` | Matching process names are displayed |
| TC07 | Process control | Start a temporary process with `sleep 300 &`; test `SIGSTOP`, `SIGCONT`, then `SIGTERM` using its PID | Process stops, resumes, and receives the termination request |
| TC08 | Live monitoring | Start `sleep 60 &`; enter its PID in Monitor Process | Process information refreshes every two seconds; `Q` or `Ctrl+C` stops monitoring |
| TC09 | Invalid PID input | Enter `0`, a negative number, or non-numeric text where a PID is requested | Input is rejected with an error message |
| TC10 | Protected PID | Attempt to control PID `1` | Operation is refused |

Use only a temporary test process for signal testing. Do not experiment on important system processes.

## How It Works

### Process information

ForgeShell scans numeric directories under `/proc` to identify process IDs. It reads:

- `/proc/<PID>/stat` for process identity, state, parent PID, and CPU-time counters.
- `/proc/<PID>/status` for process name, memory information, UID, and thread count.
- `/proc/stat` for total system CPU-time counters.

### CPU usage estimate

The monitor compares two consecutive samples:

```text
Process CPU time = user CPU time + system CPU time

CPU usage (%) =
    (process CPU-time difference / total system CPU-time difference)
    × number of online CPUs × 100
```

The first monitor display may show that CPU usage needs two samples. The displayed percentage is normalized so that 100% represents one fully utilized CPU core.

### Process-control safety

- PID `1` is protected from control operations.
- ForgeShell's own PID is protected.
- Confirmation is required before `SIGTERM` and `SIGKILL`.
- Signal-delivery success is not treated as proof that a process has already exited.
- Permission and process-not-found errors are reported.

## Limitations

- Designed for Linux systems and Linux `/proc` interfaces.
- Some process information may be unavailable due to permissions or process termination.
- Process-control operations depend on the current user's permissions.
- CPU usage is an estimate based on periodic samples.
- The application uses a terminal interface, not a graphical interface.

## Future Enhancements

- Add a graphical user interface.
- Add sorting and filtering of process lists.
- Add process-priority management.
- Add historical CPU and memory usage charts.
- Export process information to CSV or text files.
- Allow users to configure the monitoring interval.

## Conclusion

ForgeShell combines process listing, searching, inspection, monitoring, and control in one C-based Linux command-line application. By using the `/proc` filesystem and POSIX signals, it demonstrates practical Operating Systems and Systems Programming concepts through interaction with real Linux processes.

## References

- Linux manual page: `proc(5)`
- Linux manual page: `kill(2)`
- Linux manual page: `signal(7)`
- Linux manual pages: `opendir(3)` and `readdir(3)`
- Linux manual page: `termios(3)`
- GNU GCC documentation
