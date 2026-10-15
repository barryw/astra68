#define _GNU_SOURCE
#include "astra_storage_provider.h"
#include "astra_storage_discovery.h"

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/capability.h>
#include <linux/openat2.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

/* This is a root service launcher, never a setuid program or sudo entry point.
 * Paths and resource policy come from the trusted service configuration. */
typedef struct StorageConfig {
    uid_t uid;
    gid_t gid;
    const char *system;
    const char *uuid;
    const char *work;
    const char *sound;
    const char *seed_sound;
    uint64_t offset, length;
    size_t metadata, resources;
    uint32_t probes, timeout, workers;
    uint64_t ram_bytes, ram_inodes, ram0_bytes, ram0_inodes, tmp_bytes, tmp_inodes;
    char **command;
    int discovery;
} StorageConfig;

static volatile sig_atomic_t interrupted;
static volatile sig_atomic_t consumer;
static volatile sig_atomic_t provider_process = -1;

static void terminate(int signal_number)
{
    interrupted = signal_number;
    if (provider_process >= 0)
        (void)syscall(SYS_pidfd_send_signal, (int)provider_process, signal_number, NULL, 0u);
    else if (consumer > 0)
        (void)kill((pid_t)consumer, signal_number);
}

static int number(const char *text, uint64_t maximum, uint64_t *value)
{
    char *end;
    if (text == NULL || text[0] < '0' || text[0] > '9')
        return EINVAL;
    errno = 0;
    uintmax_t parsed = strtoumax(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed > maximum)
        return EINVAL;
    *value = (uint64_t)parsed;
    return 0;
}

static int config(int argc, char **argv, StorageConfig *out)
{
    uint64_t values[13];
    /* One exact configuration, with no implicit resource or storage fallback. */
    if (argc < 26 || strcmp(argv[1], "--uid") != 0 ||
        strcmp(argv[3], "--gid") != 0 || strcmp(argv[5], "--system") != 0 ||
        strcmp(argv[10], "--work") != 0 || strcmp(argv[12], "--sound") != 0 ||
        strcmp(argv[14], "--limits") != 0)
        return EINVAL;
    unsigned slots[] = {2, 4, 7, 8, 15, 16, 17, 18, 19, 20, 21, 22, 23};
    /* Remaining tmp limits follow ram0 limits; command starts after '--'. */
    if (argc < 32 || strcmp(argv[26], "--discovery") != 0 ||
        strcmp(argv[28], "--seed-sound") != 0 || strcmp(argv[30], "--") != 0 ||
        (strcmp(argv[29], "none") != 0 && argv[29][0] != '/') ||
        (strcmp(argv[27], "none") != 0 && strcmp(argv[27], "udev") != 0))
        return EINVAL;
    for (unsigned at = 0; at < sizeof(slots) / sizeof(slots[0]); ++at)
        if (number(argv[slots[at]], UINT64_MAX, &values[at]) != 0)
            return EINVAL;
    uint64_t tmp_bytes, tmp_inodes;
    if (number(argv[24], UINT64_MAX, &tmp_bytes) != 0 ||
        number(argv[25], UINT64_MAX, &tmp_inodes) != 0 ||
        values[0] == 0u || values[0] >= UINT32_MAX ||
        values[1] == 0u || values[1] >= UINT32_MAX ||
        values[4] == 0u || values[4] > SIZE_MAX || values[5] == 0u || values[5] > SIZE_MAX ||
        values[6] == 0u || values[6] > UINT32_MAX ||
        values[7] == 0u || values[7] > UINT32_MAX ||
        values[8] == 0u || values[8] > UINT32_MAX ||
        values[9] == 0u || values[10] == 0u || values[11] == 0u || values[12] == 0u ||
        tmp_bytes == 0u || tmp_inodes == 0u ||
        values[11] > values[9] || tmp_bytes > values[9] - values[11] ||
        values[12] > values[10] || tmp_inodes > values[10] - values[12] ||
        argv[6][0] != '/' || argv[11][0] != '/' || argv[13][0] != '/' ||
        argv[9][0] == '\0' || strlen(argv[9]) >= sizeof(((AstraStorageRecord *)0)->uuid))
        return EINVAL;
    *out = (StorageConfig){.uid = (uid_t)values[0], .gid = (gid_t)values[1],
        .system = argv[6], .offset = values[2], .length = values[3], .uuid = argv[9],
        .work = argv[11], .sound = argv[13], .metadata = (size_t)values[4],
        .resources = (size_t)values[5], .probes = (uint32_t)values[6],
        .timeout = (uint32_t)values[7], .workers = (uint32_t)values[8],
        .ram_bytes = values[9], .ram_inodes = values[10],
        .ram0_bytes = values[11], .ram0_inodes = values[12],
        .tmp_bytes = tmp_bytes, .tmp_inodes = tmp_inodes, .command = &argv[31],
        .seed_sound = strcmp(argv[29], "none") == 0 ? NULL : argv[29],
        .discovery = strcmp(argv[27], "udev") == 0};
    return 0;
}

static int trusted_open(const char *path, int flags)
{
    struct open_how how = {.flags = (uint64_t)(flags | O_CLOEXEC),
        .resolve = RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS};
    return (int)syscall(SYS_openat2, AT_FDCWD, path, &how, sizeof(how));
}

static int await_state(AstraStorageProvider *provider, uint64_t id, uint32_t state)
{
    while (!interrupted) {
        int result = astra_storage_provider_poll(provider);
        if (result < 0)
            return -result;
        const AstraStorageRecord *record = astra_storage_find(&provider->catalog, id);
        if (record == NULL)
            return ESTALE;
        if (record->state == state)
            return 0;
        if (record->state == ASTRA_VOLUME_FAILED || record->state == ASTRA_VOLUME_LOST)
            return record->error != 0 ? record->error : ENODEV;
        result = astra_storage_provider_wait(provider, INT_MAX);
        if (result != 0 && result != EINTR)
            return result;
    }
    return EINTR;
}

static int mount_source(AstraStorageProvider *provider, uint64_t source,
                         uint32_t flags, uint64_t bytes, uint64_t inodes)
{
    uint64_t mounted;
    const AstraStorageRecord *record = astra_storage_find(&provider->catalog, source);
    if (record == NULL)
        return ESTALE;
    int error = astra_storage_provider_mount(provider, source, record->generation,
        flags, bytes, inodes, &mounted);
    return error != 0 ? error : await_state(provider, mounted, ASTRA_VOLUME_MOUNTED);
}

static int provision(AstraStorageProvider *provider, const StorageConfig *settings)
{
    struct stat info;
    uint64_t source, binding;
    int fd = trusted_open(settings->system, O_RDWR);
    if (fd < 0)
        return errno;
    int error = fstat(fd, &info) != 0 ? errno :
        !S_ISREG(info.st_mode) || info.st_size <= 0 ||
        (uint64_t)info.st_size > (UINT64_C(4) << 30) ? EINVAL : 0;
    if (error == 0)
        error = astra_storage_provider_system(provider, fd, settings->offset,
            settings->length, &source);
    close(fd);
    if (error == 0)
        error = await_state(provider, source, ASTRA_VOLUME_UNMOUNTED);
    if (error != 0)
        return error;
    const AstraStorageRecord *record = astra_storage_find(&provider->catalog, source);
    if (record->format != ASTRA_VOLUME_EXT4 || strcmp(record->uuid, settings->uuid) != 0)
        return ENODEV;
    error = mount_source(provider, source, ASTRA_VOLUME_SYSTEM, 0u, 0u);
    const char *paths[] = {settings->work, settings->sound};
    const char *names[] = {"/work", "/sound"};
    for (unsigned at = 0u; error == 0 && at < 2u; ++at) {
        fd = trusted_open(paths[at], O_PATH | O_DIRECTORY);
        if (fd < 0)
            return errno;
        error = astra_storage_provider_export(provider, fd, names[at], 0u, &binding);
        close(fd);
        if (error == 0 && at == 1u && settings->seed_sound != NULL) {
            const AstraStorageRecord *bound = astra_storage_find(&provider->catalog, binding);
            int root = -1;
            fd = trusted_open(settings->seed_sound, O_RDONLY | O_DIRECTORY);
            error = fd < 0 ? errno : astra_storage_provider_root(provider, binding, bound->generation, &root);
            if (error == 0) error = astra_storage_path_seed(root, fd, "/soundfonts");
            if (root >= 0) close(root);
            if (fd >= 0) close(fd);
        }
    }
    for (unsigned at = 0u; error == 0 && at < 2u; ++at) {
        error = astra_storage_ram_source(&provider->catalog,
            at == 0u ? ASTRA_VOLUME_CONFIGURED_RAM : 0u, &source);
        if (error == 0)
            error = mount_source(provider, source, at == 0u ? 0u : ASTRA_VOLUME_TEMPORARY,
                at == 0u ? settings->ram0_bytes : settings->tmp_bytes,
                at == 0u ? settings->ram0_inodes : settings->tmp_inodes);
    }
    return error;
}

static void child(AstraStorageProvider *provider, const StorageConfig *settings,
                    int control, int path, pid_t parent)
{
    /* Reserve copies above the two protocol slots before rearranging FDs. */
    int a = fcntl(control, F_DUPFD_CLOEXEC, 5);
    int b = fcntl(path, F_DUPFD_CLOEXEC, 5);
    if (a < 0 || b < 0 || dup2(a, 3) < 0 || dup2(b, 4) < 0 ||
        syscall(SYS_close_range, 5u, UINT_MAX, 0u) != 0)
        _exit(126);
    for (int cap = 0; ; ++cap) {
        int present = prctl(PR_CAPBSET_READ, cap, 0, 0, 0);
        if (present < 0) {
            if (errno == EINVAL)
                break;
            _exit(126);
        }
        if (prctl(PR_CAPBSET_DROP, cap, 0, 0, 0) != 0)
            _exit(126);
    }
    struct __user_cap_header_struct header = {_LINUX_CAPABILITY_VERSION_3, 0};
    struct __user_cap_data_struct caps[2] = {{0}, {0}};
    if (setgroups(0u, NULL) != 0 || setresgid(settings->gid, settings->gid, settings->gid) != 0 ||
        setresuid(settings->uid, settings->uid, settings->uid) != 0 ||
        syscall(SYS_capset, &header, caps) != 0 ||
        prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 ||
        prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0) != 0 || getppid() != parent ||
        sigprocmask(SIG_SETMASK, &provider->previous_signals, NULL) != 0 ||
        setenv("ASTRA_STORAGE_CONTROL_FD", "3", 1) != 0 ||
        setenv("ASTRA_STORAGE_PATH_FD", "4", 1) != 0)
        _exit(126);
    execvp(settings->command[0], settings->command);
    _exit(errno == ENOENT ? 127 : 126);
}

static int run_provider(const StorageConfig *configuration, AstraStorageDomain *domain)
{
    StorageConfig settings = *configuration;
    AstraStorageProvider provider = ASTRA_STORAGE_PROVIDER_INIT;
    AstraStorageDiscovery discovery = ASTRA_STORAGE_DISCOVERY_INIT;
    int control[2] = {-1, -1}, path[2] = {-1, -1};
    int status = 0, opened = 0;
    int error = astra_storage_provider_open(&provider, domain,
        settings.metadata, settings.resources, settings.probes, settings.timeout,
        settings.workers, settings.ram_bytes, settings.ram_inodes);
    if (error == 0) {
        opened = 1;
        error = provision(&provider, &settings);
    }
    if (error == 0 && settings.discovery) {
        error = astra_storage_discovery_open(&discovery, &provider, settings.metadata);
        if (error == 0)
            provider.discovery = &discovery;
    }
    if (error == 0 && (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, control) != 0 ||
        socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, path) != 0))
        error = errno;
    int credentials = 1;
    if (error == 0 && (setsockopt(control[0], SOL_SOCKET, SO_PASSCRED, &credentials, sizeof(credentials)) != 0 ||
        setsockopt(path[0], SOL_SOCKET, SO_PASSCRED, &credentials, sizeof(credentials)) != 0))
        error = errno;
    if (error == 0 && !interrupted) {
        pid_t parent = getpid();
        pid_t pid = fork();
        if (pid == 0)
            child(&provider, &settings, control[1], path[1], parent);
        if (pid < 0)
            error = errno;
        else {
            consumer = pid;
            printf("ASTRA-STORAGE-PEER %ld\n", (long)pid);
            fflush(stdout);
            close(control[1]); control[1] = -1;
            close(path[1]); path[1] = -1;
            if (interrupted)
                (void)kill(pid, interrupted);
            error = astra_storage_provider_serve(&provider, control[0], path[0], pid);
            if (error != 0)
                (void)kill(pid, SIGTERM);
            while (waitpid(pid, &status, 0) < 0) {
                if (errno != EINTR) {
                    error = errno;
                    break;
                }
            }
            consumer = 0;
            /* EOF and pidfd readiness can arrive in either order. Child exit
             * status still decides success; a live peer closing transport is
             * terminated above and reports its real signal status. */
            if (error == EPIPE)
                error = 0;
        }
    }
    for (unsigned at = 0; at < 2u; ++at) {
        if (control[at] >= 0) close(control[at]);
        if (path[at] >= 0) close(path[at]);
    }
    if (error != 0)
        fprintf(stderr, "astra-storage: %s\n", strerror(error));
    provider.discovery = NULL;
    astra_storage_discovery_close(&discovery);
    if (opened) {
        int cleanup;
        while ((cleanup = astra_storage_provider_shutdown(&provider)) == EAGAIN) {
            int waited = astra_storage_provider_wait(&provider, 100);
            if (waited != 0 && waited != EINTR) {
                cleanup = waited;
                break;
            }
        }
        if (cleanup != 0) {
            fprintf(stderr, "astra-storage cleanup retained: %s\n", strerror(cleanup));
            return 1;
        }
    }
    if (error != 0 || interrupted)
        return 1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

int main(int argc, char **argv)
{
    StorageConfig settings;
    if (geteuid() != 0 || getuid() != 0 || config(argc, argv, &settings) != 0) {
        fputs("usage (root service only): astra-storage --uid UID --gid GID "
            "--system IMAGE OFFSET LENGTH UUID --work DIR --sound DIR "
            "--limits METADATA RESOURCES PROBES TIMEOUT_MS WORKERS RAM_BYTES RAM_INODES "
            "RAM0_BYTES RAM0_INODES TMP_BYTES TMP_INODES --discovery none|udev "
            "--seed-sound none|DIRECTORY -- COMMAND [ARGS...]\n", stderr);
        return 2;
    }
    struct sigaction action = {.sa_handler = terminate};
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, NULL) != 0 || sigaction(SIGHUP, &action, NULL) != 0 ||
        sigaction(SIGINT, &action, NULL) != 0 || prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) != 0)
        return 1;
    /* The launcher owns this inode before any provider operation can fail.
     * Only its child enters the private mount namespace. Kernel child ownership
     * retains this directory through interrupted provider/worker retirement. */
    AstraStorageDomain domain = {.directory = -1};
    int gate[2] = {-1, -1};
    int error = astra_storage_domain_open(&domain, settings.uid, settings.gid);
    int status = 0, child_status = 0, child_seen = 0;
    pid_t provider = -1;
    if (error == 0 && socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, gate) != 0)
        error = errno;
    if (error == 0 && !interrupted) {
        pid_t parent = getpid();
        provider = fork();
        if (provider < 0) error = errno;
        else if (provider == 0) {
            close(gate[0]);
            char ready;
            if (prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0) != 0 || getppid() != parent ||
                recv(gate[1], &ready, 1u, 0) != 1 || ready != 'R') _exit(126);
            close(gate[1]);
            _exit(run_provider(&settings, &domain));
        } else {
            close(gate[1]); gate[1] = -1;
            provider_process = (int)syscall(SYS_pidfd_open, provider, 0u);
            if (provider_process < 0) error = errno;
            else {
                printf("ASTRA-STORAGE-PROVIDER %ld %s\n", (long)provider, domain.path);
                fflush(stdout);
                if (interrupted) (void)syscall(SYS_pidfd_send_signal, (int)provider_process, interrupted, NULL, 0u);
                if (send(gate[0], "R", 1u, MSG_NOSIGNAL) != 1) error = errno;
            }
            close(gate[0]); gate[0] = -1;
            /* An admission failure leaves an exact unreaped child, never a
             * guessed live PID. Its closed gate prevents provider startup. */
            if (error != 0) (void)kill(provider, SIGKILL);
            for (;;) {
                pid_t retired = waitpid(-1, &status, 0);
                if (retired < 0) {
                    if (errno == EINTR) continue;
                    if (errno != ECHILD) error = errno;
                    break;
                }
                if (retired == provider) { child_status = status; child_seen = 1; }
            }
            if (provider_process >= 0) {
                int owned = provider_process; provider_process = -1; close(owned);
            }
        }
    }
    for (unsigned at = 0u; at < 2u; ++at) if (gate[at] >= 0) close(gate[at]);
    if (domain.directory >= 0) {
        int cleanup = astra_storage_domain_retire(&domain);
        if (cleanup != 0) {
            fprintf(stderr, "astra-storage domain retained: %s\n", strerror(cleanup));
            error = cleanup;
        }
    }
    if (error != 0) fprintf(stderr, "astra-storage owner: %s\n", strerror(error));
    if (error != 0 || interrupted || !child_seen) return 1;
    return WIFEXITED(child_status) ? WEXITSTATUS(child_status) : 128 + WTERMSIG(child_status);
}
