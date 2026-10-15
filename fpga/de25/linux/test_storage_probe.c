#define _GNU_SOURCE
#include "astra_storage_probe.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static AstraStorageProbeResult finish(AstraStorageProbe *job)
{
    AstraStorageProbeResult result;
    for (unsigned attempt = 0u; attempt < 10000u; ++attempt) {
        int status = astra_storage_probe_poll(job, &result);
        assert(status >= 0);
        if (status == 1) {
            assert(job->pid == 0);
            return result;
        }
        (void)poll(NULL, 0u, 1);
    }
    assert(0 && "probe never retired");
    return (AstraStorageProbeResult){0};
}

static void window_probe(const char *path, const AstraStorageProbeResult *expected)
{
    char temporary[] = "/var/tmp/astra-probe-window.XXXXXX";
    unsigned char buffer[4096];
    struct stat info;
    int source = open(path, O_RDONLY | O_CLOEXEC);
    int target = mkstemp(temporary);
    const uint64_t offset = 1024u * 1024u;
    assert(source >= 0 && target >= 0 && fstat(source, &info) == 0);
    assert(info.st_size >= 0 && ftruncate(target, (off_t)offset + info.st_size + 4096) == 0);
    off_t done = 0;
    while (done < info.st_size) {
        ssize_t got = read(source, buffer, sizeof(buffer));
        assert(got > 0 && pwrite(target, buffer, (size_t)got, (off_t)offset + done) == got);
        done += got;
    }
    close(source);
    close(target);
    target = open(temporary, O_RDONLY | O_CLOEXEC);
    AstraStorageProbe job = {0};
    assert(target >= 0 && astra_storage_probe_start(&job, target, 17u, 9u,
        getuid(), getgid(), 5000u, offset, (uint64_t)info.st_size) == 0);
    AstraStorageProbeResult result = finish(&job);
    assert(result.error == 0 && result.format == expected->format &&
        strcmp(result.uuid, expected->uuid) == 0 && strcmp(result.label, expected->label) == 0);
    assert(astra_storage_probe_start(&job, target, 17u, 9u, getuid(), getgid(),
        5000u, offset + (uint64_t)info.st_size + 4096u, 1u) == EINVAL);
    close(target);
    assert(unlink(temporary) == 0);
}

int main(int argc, char **argv)
{
    AstraStorageProbe job = {0};
    AstraStorageProbeResult result;
    int fd;
    int writable;
    assert(argc == 5);
    fd = open(argv[1], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    assert(fd >= 0);
    assert(astra_storage_probe_start(&job, fd, 1u, 1u, 0u, getgid(), 1000u, 0u, 0u) == EINVAL);
    assert(astra_storage_probe_start(&job, fd, 1u, 1u,
                                     getuid(), getgid(), 0u, 0u, 0u) == EINVAL);
    assert(astra_storage_probe_start(&job, fd, 7u, 3u,
                                     getuid(), getgid(), 5000u, 0u, 0u) == 0);
    assert(astra_storage_probe_start(&job, fd, 8u, 4u,
                                     getuid(), getgid(), 5000u, 0u, 0u) == EINVAL);
    /* Source lifetime belongs to the worker after start. */
    close(fd);
    result = finish(&job);
    assert(result.source == 7u && result.generation == 3u);
    assert(result.error == 0 && result.ambiguous == 0u);
    assert(result.format == strtoul(argv[4], NULL, 10));
    assert(strcmp(result.label, argv[2]) == 0);
    assert(strcmp(result.uuid, argv[3]) == 0);
    window_probe(argv[1], &result);
    assert(astra_storage_probe_poll(&job, &result) == -EINVAL);

    writable = open(argv[1], O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    assert(writable >= 0);
    assert(astra_storage_probe_start(&job, writable, 1u, 1u,
                                     getuid(), getgid(), 5000u, 0u, 0u) == EACCES);
    close(writable);

    fd = open(argv[1], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    assert(fd >= 0);
    assert(astra_storage_probe_start(&job, fd, 9u, 4u,
                                     getuid(), getgid(), 5000u, 0u, 0u) == 0);
    astra_storage_probe_cancel(&job);
    result = finish(&job);
    assert(result.error == ECANCELED);
    assert(result.source == 9u && result.generation == 4u);

    assert(astra_storage_probe_start(&job, fd, 10u, 5u,
                                     getuid(), getgid(), 5000u, 0u, 0u) == 0);
    job.deadline_ns = 1u;
    result = finish(&job);
    assert(result.error == ETIMEDOUT);
    close(fd);
    puts("storage probe: real identity, read-only source, cancellation, timeout, retirement PASS");
    return 0;
}
