#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if ((argc != 4 && argc != 5) || (strcmp(argv[1], "fat") && strcmp(argv[1], "exfat") &&
                      strcmp(argv[1], "mbr") && strcmp(argv[1], "partition-init") &&
                      strcmp(argv[1], "partition-append") && strcmp(argv[1], "partition-delete")))
        return 2;
    int fd = open(argv[2], O_RDWR | O_EXCL | O_CLOEXEC);
    if (fd < 0) { perror("exclusive open"); return 3; }
    if (fd != 3) {
        if (dup2(fd, 3) < 0) return 4;
        close(fd);
    }
    int flags = fcntl(3, F_GETFD);
    if (flags < 0 || fcntl(3, F_SETFD, flags & ~FD_CLOEXEC) < 0) return 5;
    pid_t child = fork();
    if (child < 0) return 6;
    if (child == 0) {
        if (argc == 5) {
            char *uid_end, *gid_end;
            const char *uid_text = getenv("SUDO_UID"), *gid_text = getenv("SUDO_GID");
            if (uid_text == NULL || gid_text == NULL) _exit(125);
            errno = 0;
            unsigned long uid = strtoul(uid_text, &uid_end, 10);
            unsigned long gid = strtoul(gid_text, &gid_end, 10);
            if (errno || *uid_end || *gid_end || uid == 0 || gid == 0 ||
                uid > UINT_MAX || gid > UINT_MAX ||
                setgroups(0, NULL) != 0 || setresgid((gid_t)gid, (gid_t)gid, (gid_t)gid) != 0 ||
                setresuid((uid_t)uid, (uid_t)uid, (uid_t)uid) != 0 || geteuid() == 0)
                _exit(125);
        }
        if (!strcmp(argv[1], "partition-init"))
            execl(argv[3], argv[3], "--no-reread", "--no-tell-kernel",
                  "--wipe", "always", "/proc/self/fd/3", (char *)NULL);
        else if (!strcmp(argv[1], "partition-append"))
            execl(argv[3], argv[3], "--append", "--no-reread", "--no-tell-kernel",
                  "--wipe", "never", "--wipe-partitions", "never",
                  "/proc/self/fd/3", (char *)NULL);
        else if (!strcmp(argv[1], "partition-delete"))
            execl(argv[3], argv[3], "--delete", "--no-reread", "--no-tell-kernel",
                  "/proc/self/fd/3", "1", (char *)NULL);
        else if (!strcmp(argv[1], "mbr"))
            execl(argv[3], argv[3], "--no-reread", "--force", "--wipe", "always",
                  "--label", "dos", "/proc/self/fd/3", (char *)NULL);
        else if (!strcmp(argv[1], "fat")) {
            if (argc == 5)
                execlp("qemu-aarch64", "qemu-aarch64", "-L", argv[4], argv[3],
                       "-F", "32", "-n", "ASTRA_TEST", "--astra-fd", "3", (char *)NULL);
            else
                execl(argv[3], argv[3], "-F", "32", "-n", "ASTRA_TEST",
                      "--astra-fd", "3", (char *)NULL);
        } else if (argc == 5)
            execlp("qemu-aarch64", "qemu-aarch64", "-L", argv[4], argv[3],
                   "-L", "ASTRA_TEST", "-P", "none", "--astra-fd", "3", (char *)NULL);
        else
            execl(argv[3], argv[3], "-L", "ASTRA_TEST", "-P", "none",
                  "--astra-fd", "3", (char *)NULL);
        perror("exec formatter");
        _exit(127);
    }
    int status;
    if (waitpid(child, &status, 0) != child) return 7;
    close(3);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 8;
}
