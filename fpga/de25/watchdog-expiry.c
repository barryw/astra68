/* Stop Linux PID 1 long enough to test DE25's systemd-owned hardware watchdog.
 * Run as a transient root service with a RuntimeMaxSec safety limit.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <time.h>

int main(int argc, char **argv)
{
    char *end;
    long seconds;
    int status;
    struct timespec delay;

    if (argc != 2) {
        fprintf(stderr, "usage: %s seconds (1..180)\n", argv[0]);
        return 2;
    }
    errno = 0;
    seconds = strtol(argv[1], &end, 10);
    if (errno || *end || seconds < 1 || seconds > 180) {
        fprintf(stderr, "invalid watchdog expiry duration\n");
        return 2;
    }
    if (ptrace(PTRACE_ATTACH, 1, NULL, NULL) < 0) {
        perror("ptrace attach pid 1");
        return 1;
    }
    if (waitpid(1, &status, 0) != 1) {
        perror("wait for pid 1 stop");
        ptrace(PTRACE_DETACH, 1, NULL, NULL);
        return 1;
    }
    if (!WIFSTOPPED(status)) {
        fprintf(stderr, "pid 1 did not stop\n");
        ptrace(PTRACE_DETACH, 1, NULL, NULL);
        return 1;
    }
    fprintf(stderr, "watchdog expiry: pid 1 stopped for %ld seconds\n", seconds);
    delay.tv_sec = seconds;
    delay.tv_nsec = 0;
    while (nanosleep(&delay, &delay) < 0) {
        if (errno == EINTR)
            continue;
        perror("nanosleep");
        ptrace(PTRACE_DETACH, 1, NULL, NULL);
        return 1;
    }
    if (ptrace(PTRACE_DETACH, 1, NULL, NULL) < 0) {
        perror("ptrace detach pid 1");
        return 1;
    }
    fprintf(stderr, "watchdog expiry: pid 1 resumed without reset\n");
    return 0;
}
