#define _GNU_SOURCE
#include "astra_codec_sandbox.h"

#include <errno.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <sys/prctl.h>
#include <sys/syscall.h>

#if defined(__aarch64__)
#define CODEC_AUDIT_ARCH AUDIT_ARCH_AARCH64
#elif defined(__x86_64__)
#define CODEC_AUDIT_ARCH AUDIT_ARCH_X86_64
#else
#error Unsupported codec worker host architecture
#endif

#define DENY_SYSCALL(number) \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (number), 0, 1), \
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM)

int astra_codec_sandbox_enter(void)
{
    /* Spawn closes all descriptors except the source socket. No parser gets
     * filesystem, network, process creation, or ptrace authority. */
    struct sock_filter rules[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                 offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, CODEC_AUDIT_ARCH, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                 offsetof(struct seccomp_data, nr)),
#ifdef __NR_open
        DENY_SYSCALL(__NR_open),
#endif
        DENY_SYSCALL(__NR_openat),
#ifdef __NR_openat2
        DENY_SYSCALL(__NR_openat2),
#endif
#ifdef __NR_creat
        DENY_SYSCALL(__NR_creat),
#endif
        DENY_SYSCALL(__NR_socket),
        DENY_SYSCALL(__NR_socketpair),
        DENY_SYSCALL(__NR_connect),
        DENY_SYSCALL(__NR_bind),
        DENY_SYSCALL(__NR_listen),
#ifdef __NR_accept
        DENY_SYSCALL(__NR_accept),
#endif
#ifdef __NR_accept4
        DENY_SYSCALL(__NR_accept4),
#endif
        DENY_SYSCALL(__NR_execve),
#ifdef __NR_execveat
        DENY_SYSCALL(__NR_execveat),
#endif
        DENY_SYSCALL(__NR_clone),
#ifdef __NR_clone3
        DENY_SYSCALL(__NR_clone3),
#endif
#ifdef __NR_fork
        DENY_SYSCALL(__NR_fork),
#endif
#ifdef __NR_vfork
        DENY_SYSCALL(__NR_vfork),
#endif
        DENY_SYSCALL(__NR_ptrace),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)
    };
    struct sock_fprog program = {
        .len = sizeof(rules) / sizeof(rules[0]),
        .filter = rules
    };

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
        return -errno;
    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) != 0)
        return -errno;
    return 0;
}
