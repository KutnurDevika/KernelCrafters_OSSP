/*
 * ForgeShell - Linux Process Monitoring and Control System
 *
 * A terminal-based (plain printf/fgets) process manager that works on
 * real processes through the /proc filesystem, kill() and POSIX signals.
 *
 * Platform : Ubuntu Linux 24.04
 * Standard : C11
 * Build    : gcc -Wall -Wextra -Wpedantic -std=c11 main.c -o shellforge
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

#define PROC_ROOT            "/proc"
#define INPUT_BUF_SIZE       128
#define NAME_BUF_SIZE        256
#define PATH_BUF_SIZE        64
#define LINE_BUF_SIZE        2048
#define MONITOR_INTERVAL_SEC 2

#define SEPARATOR  "============================================="
#define TABLE_LINE "------------------------------------------------------------"

/* ------------------------------------------------------------------ */
/* Data types                                                          */
/* ------------------------------------------------------------------ */

/* Data parsed from /proc/<pid>/stat */
typedef struct {
    pid_t    pid;
    pid_t    ppid;
    char     state;
    char     name[NAME_BUF_SIZE];
    uint64_t utime; /* user-mode CPU time, in clock ticks   */
    uint64_t stime; /* kernel-mode CPU time, in clock ticks */
} ProcStat;

/* Data parsed from /proc/<pid>/status. A value of -1 means "not available". */
typedef struct {
    char name[NAME_BUF_SIZE];
    long vm_size_kb;
    long vm_rss_kb;
    long uid;
    long threads;
} ProcStatus;

/* ------------------------------------------------------------------ */
/* Global state (needed by the signal handler and terminal restore)    */
/* ------------------------------------------------------------------ */

static volatile sig_atomic_t g_stop_requested = 0;
static struct termios        g_saved_termios;
static int                   g_termios_saved = 0;

/* ------------------------------------------------------------------ */
/* Terminal and signal helpers                                         */
/* ------------------------------------------------------------------ */

static void on_sigint(int signo)
{
    (void)signo;
    g_stop_requested = 1;
}

/* Put the terminal back exactly as we found it. Safe to call repeatedly. */
static void restore_terminal(void)
{
    if (g_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios);
        g_termios_saved = 0;
    }
}

/* Disable line buffering and echo so a single key (Q) can be read. */
static void enable_raw_input(void)
{
    struct termios raw;

    if (!isatty(STDIN_FILENO)) {
        return;
    }
    if (tcgetattr(STDIN_FILENO, &g_saved_termios) != 0) {
        return;
    }
    raw = g_saved_termios;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO); /* ISIG stays on: Ctrl+C works */
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSADRAIN, &raw) == 0) {
        g_termios_saved = 1;
    }
}

/* ------------------------------------------------------------------ */
/* Input helpers                                                       */
/* ------------------------------------------------------------------ */

static void trim_in_place(char *s)
{
    size_t len = strlen(s);
    size_t start = 0;

    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
    while (s[start] != '\0' && isspace((unsigned char)s[start])) {
        start++;
    }
    if (start > 0) {
        memmove(s, s + start, len - start + 1);
    }
}

/*
 * Read one line from stdin safely. Over-long lines are truncated and the
 * rest of the line is discarded. Returns 0 on success, -1 on EOF/error.
 */
static int read_line(const char *prompt, char *buf, size_t size)
{
    size_t len;

    if (prompt != NULL) {
        fputs(prompt, stdout);
        fflush(stdout);
    }
    if (fgets(buf, (int)size, stdin) == NULL) {
        buf[0] = '\0';
        return -1;
    }
    len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    } else {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) {
            /* discard the remainder of an over-long line */
        }
    }
    trim_in_place(buf);
    return 0;
}

static void pause_for_enter(void)
{
    char tmp[INPUT_BUF_SIZE];
    read_line("\nPress Enter to return to the main menu...", tmp, sizeof(tmp));
}

/* True if s is non-empty and made only of decimal digits. */
static int is_numeric(const char *s)
{
    if (*s == '\0') {
        return 0;
    }
    for (; *s != '\0'; s++) {
        if (!isdigit((unsigned char)*s)) {
            return 0;
        }
    }
    return 1;
}

/* Strictly parse a positive PID. Returns 0 on success, -1 if invalid. */
static int parse_pid(const char *s, pid_t *out)
{
    char *end = NULL;
    long value;

    if (!is_numeric(s)) {
        return -1;
    }
    errno = 0;
    value = strtol(s, &end, 10);
    if (errno != 0 || *end != '\0' || value <= 0 || value > INT_MAX) {
        return -1;
    }
    *out = (pid_t)value;
    return 0;
}

/* Prompt for a PID. Returns 0 on success, -1 on invalid input or EOF. */
static int prompt_pid(pid_t *out)
{
    char input[INPUT_BUF_SIZE];

    if (read_line("Enter PID: ", input, sizeof(input)) != 0) {
        return -1;
    }
    if (parse_pid(input, out) != 0) {
        printf("Error: \"%s\" is not a valid PID (a positive whole number is required).\n",
               input);
        return -1;
    }
    return 0;
}

static int is_yes(const char *s)
{
    return strcmp(s, "y") == 0 || strcmp(s, "Y") == 0 ||
           strcmp(s, "yes") == 0 || strcmp(s, "YES") == 0 ||
           strcmp(s, "Yes") == 0;
}

/* Case-insensitive substring test. */
static int contains_ci(const char *haystack, const char *needle)
{
    size_t hlen = strlen(haystack);
    size_t nlen = strlen(needle);
    size_t i, j;

    if (nlen == 0) {
        return 1;
    }
    if (nlen > hlen) {
        return 0;
    }
    for (i = 0; i + nlen <= hlen; i++) {
        for (j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i + j]) !=
                tolower((unsigned char)needle[j])) {
                break;
            }
        }
        if (j == nlen) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* /proc readers                                                       */
/* ------------------------------------------------------------------ */

static const char *state_description(char state)
{
    switch (state) {
    case 'R': return "Running";
    case 'S': return "Sleeping";
    case 'D': return "Disk sleep";
    case 'Z': return "Zombie";
    case 'T': return "Stopped";
    case 't': return "Tracing stop";
    case 'X':
    case 'x': return "Dead";
    case 'I': return "Idle";
    case 'K': return "Wakekill";
    case 'W': return "Waking";
    case 'P': return "Parked";
    default:  return "Unknown";
    }
}

static void report_proc_error(pid_t pid, int err)
{
    switch (err) {
    case ENOENT:
    case ESRCH:
        printf("Error: process %d does not exist (it may have just exited).\n", (int)pid);
        break;
    case EACCES:
    case EPERM:
        printf("Error: permission denied while reading information for PID %d.\n", (int)pid);
        break;
    default:
        printf("Error: cannot read information for PID %d: %s\n", (int)pid, strerror(err));
        break;
    }
}

/*
 * Parse one line of /proc/<pid>/stat.
 * The command name sits between the first '(' and the LAST ')', and may itself
 * contain spaces or parentheses, so it is located before the other fields.
 */
static int parse_stat_line(const char *line, ProcStat *out)
{
    const char *open_paren  = strchr(line, '(');
    const char *close_paren = strrchr(line, ')');
    size_t name_len;
    int pid_val = 0;
    int ppid_val = 0;
    char state = '?';
    unsigned long utime = 0;
    unsigned long stime = 0;

    if (open_paren == NULL || close_paren == NULL || close_paren < open_paren) {
        errno = EINVAL;
        return -1;
    }
    if (sscanf(line, "%d", &pid_val) != 1) {
        errno = EINVAL;
        return -1;
    }

    name_len = (size_t)(close_paren - open_paren - 1);
    if (name_len >= sizeof(out->name)) {
        name_len = sizeof(out->name) - 1;
    }
    memcpy(out->name, open_paren + 1, name_len);
    out->name[name_len] = '\0';

    /*
     * Fields after ")": state(3) ppid(4) pgrp session tty_nr tpgid flags
     * minflt cminflt majflt cmajflt utime(14) stime(15)
     */
    if (sscanf(close_paren + 1,
               " %c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu",
               &state, &ppid_val, &utime, &stime) != 4) {
        errno = EINVAL;
        return -1;
    }

    out->pid   = (pid_t)pid_val;
    out->ppid  = (pid_t)ppid_val;
    out->state = state;
    out->utime = (uint64_t)utime;
    out->stime = (uint64_t)stime;
    return 0;
}

/* Read /proc/<pid>/stat. Returns 0 on success, -1 on failure with errno set. */
static int read_proc_stat(pid_t pid, ProcStat *out)
{
    char path[PATH_BUF_SIZE];
    char line[LINE_BUF_SIZE];
    FILE *fp;
    int saved;

    snprintf(path, sizeof(path), PROC_ROOT "/%d/stat", (int)pid);
    fp = fopen(path, "r");
    if (fp == NULL) {
        return -1; /* errno already set by fopen */
    }
    errno = 0;
    if (fgets(line, sizeof(line), fp) == NULL) {
        saved = (errno != 0) ? errno : ESRCH; /* empty read => process is gone */
        fclose(fp);
        errno = saved;
        return -1;
    }
    fclose(fp);
    return parse_stat_line(line, out);
}

static void init_status(ProcStatus *s)
{
    s->name[0]    = '\0';
    s->vm_size_kb = -1;
    s->vm_rss_kb  = -1;
    s->uid        = -1;
    s->threads    = -1;
}

/* If line starts with key, parse the first number after it into *out. */
static int parse_long_field(const char *line, const char *key, long *out)
{
    size_t key_len = strlen(key);

    if (strncmp(line, key, key_len) != 0) {
        return 0;
    }
    return sscanf(line + key_len, "%ld", out) == 1;
}

/*
 * Read /proc/<pid>/status. Fields that are missing (for example VmSize and
 * VmRSS on kernel threads) stay at -1. Returns 0 on success, -1 on failure.
 */
static int read_proc_status(pid_t pid, ProcStatus *out)
{
    char path[PATH_BUF_SIZE];
    char line[LINE_BUF_SIZE];
    FILE *fp;

    init_status(out);
    snprintf(path, sizeof(path), PROC_ROOT "/%d/status", (int)pid);
    fp = fopen(path, "r");
    if (fp == NULL) {
        return -1;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strncmp(line, "Name:", 5) == 0) {
            const char *p = line + 5;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            snprintf(out->name, sizeof(out->name), "%s", p);
            out->name[strcspn(out->name, "\n")] = '\0';
            continue;
        }
        if (parse_long_field(line, "VmSize:", &out->vm_size_kb)) continue;
        if (parse_long_field(line, "VmRSS:",  &out->vm_rss_kb))  continue;
        if (parse_long_field(line, "Uid:",    &out->uid))        continue;
        if (parse_long_field(line, "Threads:", &out->threads))   continue;
    }
    fclose(fp);
    return 0;
}

/*
 * Sum of all CPU time counters on the first line of /proc/stat
 * (user nice system idle iowait irq softirq steal), in clock ticks.
 * guest/guest_nice are excluded because they are already counted in user/nice.
 */
static int read_total_cpu_time(uint64_t *total)
{
    char line[LINE_BUF_SIZE];
    unsigned long long v[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint64_t sum = 0;
    FILE *fp;
    int n, i;

    fp = fopen(PROC_ROOT "/stat", "r");
    if (fp == NULL) {
        return -1;
    }
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        errno = EIO;
        return -1;
    }
    fclose(fp);

    n = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
               &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
    if (n < 4) {
        errno = EINVAL;
        return -1;
    }
    for (i = 0; i < n; i++) {
        sum += (uint64_t)v[i];
    }
    *total = sum;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Output helpers                                                      */
/* ------------------------------------------------------------------ */

static void print_heading(const char *title)
{
    printf("\n%s\n  %s\n%s\n", SEPARATOR, title, SEPARATOR);
}

static void print_menu(void)
{
    printf("\n%s\n", SEPARATOR);
    printf("              FORGESHELL\n");
    printf("     LINUX PROCESS MONITORING SYSTEM\n");
    printf("%s\n\n", SEPARATOR);
    printf("1. List Running Processes\n");
    printf("2. Search Process\n");
    printf("3. Inspect Process\n");
    printf("4. Control Process\n");
    printf("5. Monitor Process\n");
    printf("6. Exit\n\n");
    printf("%s\n", SEPARATOR);
}

static void print_table_header(void)
{
    printf("%-8s %-8s %-14s %s\n", "PID", "PPID", "STATE", "NAME");
    printf("%s\n", TABLE_LINE);
}

static void print_proc_row(const ProcStat *ps)
{
    char state_text[32];

    snprintf(state_text, sizeof(state_text), "%c (%s)", ps->state,
             state_description(ps->state));
    printf("%-8d %-8d %-14s %s\n", (int)ps->pid, (int)ps->ppid, state_text, ps->name);
}

static void print_kb_value(const char *label, long kb)
{
    if (kb < 0) {
        printf("%-22s: N/A (not available for this process)\n", label);
    } else {
        printf("%-22s: %ld kB (%.2f MB)\n", label, kb, (double)kb / 1024.0);
    }
}

/* ------------------------------------------------------------------ */
/* Feature 1: List running processes                                   */
/* ------------------------------------------------------------------ */

static void list_processes(void)
{
    DIR *dir;
    struct dirent *entry;
    int shown = 0;
    int vanished = 0;

    print_heading("RUNNING PROCESSES");

    dir = opendir(PROC_ROOT);
    if (dir == NULL) {
        printf("Error: cannot open %s: %s\n", PROC_ROOT, strerror(errno));
        return;
    }

    print_table_header();
    while ((entry = readdir(dir)) != NULL) {
        pid_t pid;
        ProcStat ps;

        if (!is_numeric(entry->d_name) || parse_pid(entry->d_name, &pid) != 0) {
            continue; /* not a PID directory */
        }
        if (read_proc_stat(pid, &ps) != 0) {
            vanished++; /* process exited between readdir() and fopen() */
            continue;
        }
        print_proc_row(&ps);
        shown++;
    }
    closedir(dir);

    printf("%s\n", TABLE_LINE);
    printf("Total processes listed: %d", shown);
    if (vanished > 0) {
        printf(" (%d exited while being read)", vanished);
    }
    printf("\n");
}

/* ------------------------------------------------------------------ */
/* Feature 2: Search processes                                         */
/* ------------------------------------------------------------------ */

static void search_processes(void)
{
    char query[INPUT_BUF_SIZE];
    pid_t pid;
    ProcStat ps;
    DIR *dir;
    struct dirent *entry;
    int matches = 0;

    print_heading("SEARCH PROCESS");

    if (read_line("Enter PID, process name or part of a name: ", query, sizeof(query)) != 0) {
        return;
    }
    if (query[0] == '\0') {
        printf("Error: search text cannot be empty.\n");
        return;
    }

    /* A purely numeric query is treated as an exact PID lookup. */
    if (is_numeric(query)) {
        if (parse_pid(query, &pid) != 0) {
            printf("Error: \"%s\" is not a valid PID.\n", query);
            return;
        }
        if (read_proc_stat(pid, &ps) != 0) {
            if (errno == ENOENT || errno == ESRCH) {
                printf("No process matches PID %d.\n", (int)pid);
            } else {
                report_proc_error(pid, errno);
            }
            return;
        }
        printf("\n");
        print_table_header();
        print_proc_row(&ps);
        printf("%s\n1 match found.\n", TABLE_LINE);
        return;
    }

    /* Otherwise: case-insensitive exact or partial name match. */
    dir = opendir(PROC_ROOT);
    if (dir == NULL) {
        printf("Error: cannot open %s: %s\n", PROC_ROOT, strerror(errno));
        return;
    }
    printf("\n");
    while ((entry = readdir(dir)) != NULL) {
        if (!is_numeric(entry->d_name) || parse_pid(entry->d_name, &pid) != 0) {
            continue;
        }
        if (read_proc_stat(pid, &ps) != 0) {
            continue; /* vanished while searching */
        }
        if (!contains_ci(ps.name, query)) {
            continue;
        }
        if (matches == 0) {
            print_table_header();
        }
        print_proc_row(&ps);
        matches++;
    }
    closedir(dir);

    if (matches == 0) {
        printf("No process matches \"%s\".\n", query);
    } else {
        printf("%s\n%d match(es) found.\n", TABLE_LINE, matches);
    }
}

/* ------------------------------------------------------------------ */
/* Feature 3: Inspect process                                          */
/* ------------------------------------------------------------------ */

static void inspect_process(void)
{
    pid_t pid;
    ProcStat ps;
    ProcStatus st;
    int have_status;
    long ticks_per_sec;
    double user_s, sys_s;

    print_heading("INSPECT PROCESS");

    if (prompt_pid(&pid) != 0) {
        return;
    }
    if (read_proc_stat(pid, &ps) != 0) {
        report_proc_error(pid, errno);
        return;
    }
    have_status = (read_proc_status(pid, &st) == 0);
    if (!have_status) {
        if (errno == ENOENT || errno == ESRCH) {
            report_proc_error(pid, errno);
            return;
        }
        init_status(&st); /* fall back to /proc/<pid>/stat data only */
    }

    ticks_per_sec = sysconf(_SC_CLK_TCK);
    if (ticks_per_sec <= 0) {
        ticks_per_sec = 100;
    }
    user_s = (double)ps.utime / (double)ticks_per_sec;
    sys_s  = (double)ps.stime / (double)ticks_per_sec;

    printf("\n");
    printf("%-22s: %s\n", "Process name", st.name[0] != '\0' ? st.name : ps.name);
    printf("%-22s: %d\n", "PID", (int)ps.pid);
    printf("%-22s: %d\n", "Parent PID (PPID)", (int)ps.ppid);
    printf("%-22s: %c (%s)\n", "State", ps.state, state_description(ps.state));
    print_kb_value("Virtual mem (VmSize)", st.vm_size_kb);
    print_kb_value("Resident mem (VmRSS)", st.vm_rss_kb);

    if (st.uid >= 0) {
        struct passwd *pw = getpwuid((uid_t)st.uid);
        printf("%-22s: %ld (%s)\n", "User ID", st.uid, pw != NULL ? pw->pw_name : "unknown");
    } else {
        printf("%-22s: N/A\n", "User ID");
    }
    if (st.threads >= 0) {
        printf("%-22s: %ld\n", "Threads", st.threads);
    } else {
        printf("%-22s: N/A\n", "Threads");
    }
    printf("%-22s: %.2f s (user %.2f s, system %.2f s)\n", "CPU time (total)",
           user_s + sys_s, user_s, sys_s);
    if (!have_status) {
        printf("\nNote: /proc/%d/status could not be read; only stat data is shown.\n", (int)pid);
    }
}

/* ------------------------------------------------------------------ */
/* Feature 4: Process control                                          */
/* Returns 1 if ForgeShell must exit (confirmed SIGTERM), else 0.      */
/* ------------------------------------------------------------------ */

static int control_process(void)
{
    char input[INPUT_BUF_SIZE];
    const char *action = NULL;
    const char *signame = NULL;
    int sig = 0;
    int needs_confirm = 0;
    pid_t pid;
    ProcStat ps;
    int have_name;
    int err;

    print_heading("PROCESS CONTROL");
    printf("1. Terminate process  (SIGTERM - asks the process to exit cleanly)\n");
    printf("2. Force terminate    (SIGKILL - last resort; cannot be caught or ignored)\n");
    printf("3. Stop process       (SIGSTOP - pause execution)\n");
    printf("4. Continue process   (SIGCONT - resume a stopped process)\n");
    printf("5. Back to main menu\n\n");

    if (read_line("Select action: ", input, sizeof(input)) != 0) {
        return 0;
    }
    if (strlen(input) != 1) {
        printf("Error: invalid selection \"%s\".\n", input);
        return 0;
    }
    switch (input[0]) {
    case '1': action = "Terminate process";  signame = "SIGTERM"; sig = SIGTERM; needs_confirm = 1; break;
    case '2': action = "Force terminate";    signame = "SIGKILL"; sig = SIGKILL; needs_confirm = 1; break;
    case '3': action = "Stop process";       signame = "SIGSTOP"; sig = SIGSTOP; break;
    case '4': action = "Continue process";   signame = "SIGCONT"; sig = SIGCONT; break;
    case '5': return 0;
    default:
        printf("Error: invalid selection \"%s\".\n", input);
        return 0;
    }

    if (sig == SIGKILL) {
        printf("\nSIGKILL is offered separately because some processes ignore or\n"
               "handle SIGTERM. Use it only if SIGTERM has failed.\n");
    }

    if (prompt_pid(&pid) != 0) {
        return 0;
    }

    /* Safety rules: never touch PID 1 or ForgeShell itself. */
    if (pid == 1) {
        printf("Refused: PID 1 is the system init process and cannot be controlled here.\n");
        return 0;
    }
    if (pid == getpid()) {
        printf("Refused: PID %d is ForgeShell itself and cannot be controlled here.\n", (int)pid);
        return 0;
    }

    have_name = (read_proc_stat(pid, &ps) == 0);
    if (!have_name) {
        err = errno;
        if (err == ENOENT || err == ESRCH) {
            report_proc_error(pid, err);
            return 0;
        }
    }

    printf("\nSelected action : %s (%s)\n", action, signame);
    printf("Target PID      : %d\n", (int)pid);
    if (have_name) {
        printf("Process name    : %s\n", ps.name);
    }

    if (needs_confirm) {
        if (pid == getppid()) {
            printf("\nWARNING: PID %d is the parent of ForgeShell (normally your shell).\n"
                   "Terminating it may close this terminal session.\n", (int)pid);
        }
        if (read_line("\nAre you sure? Type y to confirm (y/N): ", input, sizeof(input)) != 0 ||
            !is_yes(input)) {
            printf("Cancelled. No signal was sent.\n");
            return 0;
        }
    }

    errno = 0;
    if (kill(pid, sig) == -1) {
        err = errno;
        printf("\nFAILED: could not send %s to PID %d: %s (errno %d)\n",
               signame, (int)pid, strerror(err), err);
        if (err == EPERM) {
            printf("You do not have permission to signal this process.\n");
        } else if (err == ESRCH) {
            printf("The process no longer exists.\n");
        }
        return 0;
    }

    printf("\nSUCCESS: %s was delivered to PID %d.\n", signame, (int)pid);

    if (sig == SIGTERM || sig == SIGKILL) {
        printf("Note: delivering a signal does not guarantee the process has already\n"
               "exited. It may take a moment, and a process can handle or ignore SIGTERM.\n");
    }
    if (sig == SIGSTOP) {
        printf("Use Inspect Process to confirm that its state is now T (Stopped).\n");
    }
    if (sig == SIGCONT) {
        printf("A stopped process resumes; use Inspect Process to confirm its state.\n");
    }

    if (sig == SIGTERM) {
        printf("\nTermination request sent to PID %d. ForgeShell is now exiting.\n", (int)pid);
        fflush(stdout);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Feature 5: Live process monitoring                                  */
/* ------------------------------------------------------------------ */

/*
 * Wait up to `seconds`, watching stdin for Q. Returns 1 if monitoring should
 * stop (Q pressed, input closed or Ctrl+C), 0 when the interval has elapsed.
 */
static int wait_interval_or_quit(int seconds)
{
    struct timespec start, now;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (;;) {
        fd_set readfds;
        struct timeval tv;
        double elapsed, remaining;
        int rc;

        if (g_stop_requested) {
            return 1;
        }
        clock_gettime(CLOCK_MONOTONIC, &now);
        elapsed = (double)(now.tv_sec - start.tv_sec) +
                  (double)(now.tv_nsec - start.tv_nsec) / 1e9;
        remaining = (double)seconds - elapsed;
        if (remaining <= 0.0) {
            return 0;
        }
        tv.tv_sec  = (time_t)remaining;
        tv.tv_usec = (suseconds_t)((remaining - (double)tv.tv_sec) * 1e6);

        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        rc = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        if (rc < 0) {
            if (errno == EINTR) {
                continue; /* re-check g_stop_requested */
            }
            return 1;
        }
        if (rc > 0) {
            char ch;
            ssize_t n = read(STDIN_FILENO, &ch, 1);
            if (n <= 0 || ch == 'q' || ch == 'Q') {
                return 1;
            }
        }
    }
}

static void render_monitor(const ProcStat *ps, const ProcStatus *st,
                           int cpu_valid, double cpu_percent, long ncpu, int use_clear)
{
    if (use_clear) {
        fputs("\033[H\033[2J", stdout); /* cursor home + clear screen */
    } else {
        printf("\n");
    }
    printf("%s\n", SEPARATOR);
    printf("  LIVE PROCESS MONITOR  (refresh every %d s)\n", MONITOR_INTERVAL_SEC);
    printf("%s\n\n", SEPARATOR);
    printf("%-22s: %d\n", "PID", (int)ps->pid);
    printf("%-22s: %s\n", "Process name", st->name[0] != '\0' ? st->name : ps->name);
    printf("%-22s: %d\n", "PPID", (int)ps->ppid);
    printf("%-22s: %c (%s)\n", "State", ps->state, state_description(ps->state));
    if (st->vm_rss_kb >= 0) {
        printf("%-22s: %ld kB (%.2f MB)\n", "Memory (VmRSS)", st->vm_rss_kb,
               (double)st->vm_rss_kb / 1024.0);
    } else {
        printf("%-22s: N/A\n", "Memory (VmRSS)");
    }
    if (cpu_valid) {
        printf("%-22s: %.1f %%  (100%% = one full core, %ld core(s) online)\n",
               "CPU usage", cpu_percent, ncpu);
    } else {
        printf("%-22s: -- (needs two samples)\n", "CPU usage");
    }
    if (ps->state == 'Z') {
        printf("\nThis process is a zombie: it has exited and awaits its parent.\n");
    }
    printf("\n%s\n", SEPARATOR);
    printf("Press Q to stop monitoring (or Ctrl+C).\n");
    fflush(stdout);
}

static void monitor_process(void)
{
    pid_t pid;
    ProcStat prev, cur;
    ProcStatus st;
    uint64_t total_prev = 0, total_cur = 0;
    double cpu_percent = 0.0;
    int cpu_valid = 0;
    int exited = 0;
    int read_failed = 0;
    int use_clear;
    long ncpu;
    struct sigaction sa, old_sa;

    print_heading("MONITOR PROCESS");

    if (prompt_pid(&pid) != 0) {
        return;
    }
    if (read_proc_stat(pid, &cur) != 0) {
        report_proc_error(pid, errno);
        return;
    }
    if (read_total_cpu_time(&total_cur) != 0) {
        printf("Error: cannot read %s/stat: %s\n", PROC_ROOT, strerror(errno));
        return;
    }

    ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu < 1) {
        ncpu = 1;
    }
    use_clear = isatty(STDOUT_FILENO);

    /* Ctrl+C only stops monitoring; sa_flags = 0 lets select() return EINTR. */
    g_stop_requested = 0;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, &old_sa);

    enable_raw_input();

    for (;;) {
        if (read_proc_status(pid, &st) != 0) {
            init_status(&st);
        }
        render_monitor(&cur, &st, cpu_valid, cpu_percent, ncpu, use_clear);

        if (wait_interval_or_quit(MONITOR_INTERVAL_SEC)) {
            break;
        }

        prev = cur;
        total_prev = total_cur;

        if (read_proc_stat(pid, &cur) != 0) {
            exited = 1; /* /proc/<pid> disappeared: the process has exited */
            break;
        }
        if (read_total_cpu_time(&total_cur) != 0) {
            read_failed = 1;
            break;
        }

        /* CPU % from the difference between two consecutive readings. */
        {
            uint64_t proc_prev  = prev.utime + prev.stime;
            uint64_t proc_cur   = cur.utime + cur.stime;
            uint64_t proc_delta = (proc_cur >= proc_prev) ? proc_cur - proc_prev : 0;
            uint64_t total_delta = (total_cur > total_prev) ? total_cur - total_prev : 0;

            if (total_delta > 0) {
                cpu_percent = 100.0 * (double)ncpu * (double)proc_delta / (double)total_delta;
                cpu_valid = 1;
            } else {
                cpu_valid = 0;
            }
        }
    }

    /* Always restore signal handler and terminal behaviour. */
    sigaction(SIGINT, &old_sa, NULL);
    restore_terminal();
    if (isatty(STDIN_FILENO)) {
        tcflush(STDIN_FILENO, TCIFLUSH);
    }

    printf("\n");
    if (exited) {
        printf("Process %d has exited (its /proc entry no longer exists).\n", (int)pid);
    } else if (read_failed) {
        printf("Error: could not read %s/stat: %s\n", PROC_ROOT, strerror(errno));
    }
    printf("Monitoring stopped.\n");
}

/* ------------------------------------------------------------------ */
/* Main menu                                                           */
/* ------------------------------------------------------------------ */

int main(void)
{
    char input[INPUT_BUF_SIZE];
    int running = 1;

    atexit(restore_terminal);

    while (running) {
        print_menu();
        if (read_line("Enter your choice: ", input, sizeof(input)) != 0) {
            printf("\nInput closed. Exiting ForgeShell.\n");
            break;
        }
        if (strlen(input) != 1) {
            printf("Invalid choice \"%s\". Please enter a number from 1 to 6.\n", input);
            continue;
        }

        switch (input[0]) {
        case '1':
            list_processes();
            pause_for_enter();
            break;
        case '2':
            search_processes();
            pause_for_enter();
            break;
        case '3':
            inspect_process();
            pause_for_enter();
            break;
        case '4':
            if (control_process()) {
                running = 0; /* confirmed SIGTERM: leave ForgeShell */
            } else {
                pause_for_enter();
            }
            break;
        case '5':
            monitor_process();
            pause_for_enter();
            break;
        case '6':
            printf("\nExiting ForgeShell. Goodbye!\n");
            running = 0;
            break;
        default:
            printf("Invalid choice \"%s\". Please enter a number from 1 to 6.\n", input);
            break;
        }
    }

    return EXIT_SUCCESS;
}
