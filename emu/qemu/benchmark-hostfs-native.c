#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define DIRECT_BYTES 192u
#define JOURNAL_RECORD_BYTES 256u

static void fail(const char *operation)
{
    perror(operation);
    exit(EXIT_FAILURE);
}

static uint64_t now_ns(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        fail("clock_gettime");
    }
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) +
           (uint64_t)now.tv_nsec;
}

static void write_all(int fd, const void *buffer, size_t bytes)
{
    const uint8_t *at = buffer;

    while (bytes != 0u) {
        ssize_t moved = write(fd, at, bytes);

        if (moved < 0 && errno == EINTR) {
            continue;
        }
        if (moved <= 0) {
            fail("write");
        }
        at += (size_t)moved;
        bytes -= (size_t)moved;
    }
}

static void read_all(int fd, void *buffer, size_t bytes)
{
    uint8_t *at = buffer;

    while (bytes != 0u) {
        ssize_t moved = read(fd, at, bytes);

        if (moved < 0 && errno == EINTR) {
            continue;
        }
        if (moved <= 0) {
            fail("read");
        }
        at += (size_t)moved;
        bytes -= (size_t)moved;
    }
}

static void make_path(char *path, size_t capacity, const char *directory,
                      const char *name)
{
    int length = snprintf(path, capacity, "%s/%s", directory, name);

    if (length < 0 || (size_t)length >= capacity) {
        fputs("benchmark path is too long\n", stderr);
        exit(EXIT_FAILURE);
    }
}

static int create_durable_file(const char *directory, const char *name,
                               char *path, size_t capacity)
{
    int directory_fd;
    int fd;

    make_path(path, capacity, directory, name);
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        fail("open benchmark file");
    }
    if (fdatasync(fd) != 0) {
        fail("fdatasync new file");
    }
    directory_fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_fd < 0 || fsync(directory_fd) != 0) {
        fail("fsync benchmark directory");
    }
    if (close(directory_fd) != 0) {
        fail("close benchmark directory");
    }
    return fd;
}

static void report(const char *name, uint64_t operations, uint64_t batch,
                   uint64_t elapsed)
{
    double rate = (double)operations * 1000000000.0 / (double)elapsed;

    printf("NATIVE name=%s operations=%" PRIu64 " batch=%" PRIu64
           " elapsed-ns=%" PRIu64 " ops/s=%.2f\n",
           name, operations, batch, elapsed, rate);
}

static void durable_barrier(int fd, bool filesystem)
{
    int result = filesystem ? syncfs(fd) : fdatasync(fd);

    if (result != 0) {
        fail(filesystem ? "syncfs journal" : "fdatasync journal");
    }
}

static void fill_record(uint8_t record[JOURNAL_RECORD_BYTES],
                        uint64_t sequence)
{
    memset(record, (int)(sequence & 0xffu), JOURNAL_RECORD_BYTES);
    memcpy(record, &sequence, sizeof(sequence));
}

static void verify_journal(const char *path, uint64_t operations)
{
    uint8_t actual[JOURNAL_RECORD_BYTES];
    uint8_t expected[JOURNAL_RECORD_BYTES];
    uint8_t *seen = calloc((size_t)((operations + 7u) / 8u), 1u);
    struct stat state;
    int fd;

    if (seen == NULL) {
        fail("allocate journal verification");
    }
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0 || fstat(fd, &state) != 0 ||
        (uint64_t)state.st_size != operations * JOURNAL_RECORD_BYTES) {
        fputs("journal size verification failed\n", stderr);
        exit(EXIT_FAILURE);
    }
    for (uint64_t index = 0; index < operations; ++index) {
        uint64_t sequence;

        read_all(fd, actual, sizeof(actual));
        memcpy(&sequence, actual, sizeof(sequence));
        if (sequence >= operations ||
            (seen[sequence / 8u] & (uint8_t)(1u << (sequence & 7u))) != 0u) {
            fputs("journal sequence verification failed\n", stderr);
            exit(EXIT_FAILURE);
        }
        fill_record(expected, sequence);
        if (memcmp(actual, expected, sizeof(actual)) != 0) {
            fputs("journal data verification failed\n", stderr);
            exit(EXIT_FAILURE);
        }
        seen[sequence / 8u] |= (uint8_t)(1u << (sequence & 7u));
    }
    if (close(fd) != 0) {
        fail("close verified journal");
    }
    free(seen);
}

static void benchmark_direct(const char *directory, uint64_t operations,
                             const uint8_t data[DIRECT_BYTES])
{
    char path[4096];
    struct stat state;
    uint64_t started;
    uint64_t elapsed;
    int fd = create_durable_file(directory, "direct.bin", path, sizeof(path));

    if (close(fd) != 0) {
        fail("close new direct file");
    }
    started = now_ns();
    for (uint64_t index = 0; index < operations; ++index) {
        fd = open(path, O_WRONLY | O_TRUNC | O_CLOEXEC);
        if (fd < 0) {
            fail("open direct file");
        }
        write_all(fd, data, DIRECT_BYTES);
        if (fdatasync(fd) != 0) {
            fail("fdatasync direct file");
        }
        if (close(fd) != 0) {
            fail("close direct file");
        }
    }
    elapsed = now_ns() - started;
    if (stat(path, &state) != 0 || state.st_size != DIRECT_BYTES) {
        fputs("direct file verification failed\n", stderr);
        exit(EXIT_FAILURE);
    }
    report("rewrite-fdatasync", operations, 1u, elapsed);
}

static void benchmark_journal(const char *directory, uint64_t operations,
                              uint64_t batch, const uint8_t *records,
                              bool filesystem)
{
    char name[64];
    char path[4096];
    uint64_t started;
    uint64_t elapsed;
    uint64_t at = 0;
    int fd;

    if (snprintf(name, sizeof(name), "journal-%s-%" PRIu64 ".bin",
                 filesystem ? "syncfs" : "fdatasync", batch) < 0) {
        fail("format journal name");
    }
    fd = create_durable_file(directory, name, path, sizeof(path));
    started = now_ns();
    while (at != operations) {
        uint64_t count = operations - at;

        if (count > batch) {
            count = batch;
        }
        write_all(fd, records + at * JOURNAL_RECORD_BYTES,
                  (size_t)count * JOURNAL_RECORD_BYTES);
        durable_barrier(fd, filesystem);
        at += count;
    }
    elapsed = now_ns() - started;
    if (close(fd) != 0) {
        fail("close journal");
    }
    verify_journal(path, operations);
    report(filesystem ? "journal-syncfs" : "journal-fdatasync",
           operations, batch, elapsed);
}

typedef struct CommitRequest {
    struct CommitRequest *next;
    uint8_t record[JOURNAL_RECORD_BYTES];
    bool committed;
} CommitRequest;

typedef struct CommitQueue {
    pthread_mutex_t mutex;
    pthread_cond_t available;
    pthread_cond_t completed;
    CommitRequest *head;
    CommitRequest *tail;
    uint8_t *batch;
    uint64_t batch_capacity;
    uint64_t commits;
    uint64_t max_batch;
    int fd;
    bool stopping;
    bool filesystem;
} CommitQueue;

typedef struct Producer {
    CommitQueue *queue;
    pthread_barrier_t *start;
    _Atomic(uint64_t) *sequence;
    uint64_t operations;
} Producer;

static void wait_barrier(pthread_barrier_t *barrier)
{
    int result = pthread_barrier_wait(barrier);

    if (result != 0 && result != PTHREAD_BARRIER_SERIAL_THREAD) {
        errno = result;
        fail("wait at group commit barrier");
    }
}

static void *commit_main(void *opaque)
{
    CommitQueue *queue = opaque;

    for (;;) {
        CommitRequest *requests;
        CommitRequest *request;
        uint64_t count = 0;

        if (pthread_mutex_lock(&queue->mutex) != 0) {
            fail("lock commit queue");
        }
        while (queue->head == NULL && !queue->stopping) {
            if (pthread_cond_wait(&queue->available, &queue->mutex) != 0) {
                fail("wait for commit work");
            }
        }
        if (queue->head == NULL && queue->stopping) {
            pthread_mutex_unlock(&queue->mutex);
            return NULL;
        }
        requests = queue->head;
        queue->head = NULL;
        queue->tail = NULL;
        if (pthread_mutex_unlock(&queue->mutex) != 0) {
            fail("unlock commit queue");
        }

        for (request = requests; request != NULL; request = request->next) {
            if (count == queue->batch_capacity) {
                fputs("commit batch exceeded producer count\n", stderr);
                exit(EXIT_FAILURE);
            }
            memcpy(queue->batch + count * JOURNAL_RECORD_BYTES,
                   request->record, JOURNAL_RECORD_BYTES);
            ++count;
        }
        write_all(queue->fd, queue->batch,
                  (size_t)count * JOURNAL_RECORD_BYTES);
        durable_barrier(queue->fd, queue->filesystem);

        if (pthread_mutex_lock(&queue->mutex) != 0) {
            fail("lock commit completion");
        }
        ++queue->commits;
        if (count > queue->max_batch) {
            queue->max_batch = count;
        }
        for (request = requests; request != NULL; request = request->next) {
            request->committed = true;
        }
        if (pthread_cond_broadcast(&queue->completed) != 0 ||
            pthread_mutex_unlock(&queue->mutex) != 0) {
            fail("publish commit completion");
        }
    }
}

static void *producer_main(void *opaque)
{
    Producer *producer = opaque;
    CommitRequest request;

    memset(&request, 0, sizeof(request));
    wait_barrier(producer->start);
    for (uint64_t index = 0; index < producer->operations; ++index) {
        uint64_t sequence = atomic_fetch_add(producer->sequence, 1u);

        fill_record(request.record, sequence);
        request.next = NULL;
        request.committed = false;
        if (pthread_mutex_lock(&producer->queue->mutex) != 0) {
            fail("lock producer queue");
        }
        if (producer->queue->tail == NULL) {
            producer->queue->head = &request;
        } else {
            producer->queue->tail->next = &request;
        }
        producer->queue->tail = &request;
        if (pthread_cond_signal(&producer->queue->available) != 0) {
            fail("signal commit work");
        }
        while (!request.committed) {
            if (pthread_cond_wait(&producer->queue->completed,
                                  &producer->queue->mutex) != 0) {
                fail("wait for commit completion");
            }
        }
        if (pthread_mutex_unlock(&producer->queue->mutex) != 0) {
            fail("unlock producer queue");
        }
    }
    return NULL;
}

static void benchmark_group_commit(const char *directory,
                                   uint64_t operations, uint64_t threads,
                                   bool filesystem)
{
    char name[64];
    char path[4096];
    CommitQueue queue;
    pthread_barrier_t start;
    pthread_t commit_thread;
    pthread_t *producer_threads;
    Producer *producers;
    _Atomic(uint64_t) sequence = 0;
    uint64_t started;
    uint64_t elapsed;

    memset(&queue, 0, sizeof(queue));
    if (snprintf(name, sizeof(name), "concurrent-%s-%" PRIu64 ".bin",
                 filesystem ? "syncfs" : "fdatasync", threads) < 0) {
        fail("format concurrent journal name");
    }
    queue.fd = create_durable_file(directory, name, path, sizeof(path));
    queue.filesystem = filesystem;
    queue.batch_capacity = threads;
    queue.batch = malloc((size_t)threads * JOURNAL_RECORD_BYTES);
    producer_threads = calloc((size_t)threads, sizeof(*producer_threads));
    producers = calloc((size_t)threads, sizeof(*producers));
    if (queue.batch == NULL || producer_threads == NULL || producers == NULL) {
        fail("allocate group commit state");
    }
    if (pthread_mutex_init(&queue.mutex, NULL) != 0 ||
        pthread_cond_init(&queue.available, NULL) != 0 ||
        pthread_cond_init(&queue.completed, NULL) != 0 ||
        pthread_barrier_init(&start, NULL, (unsigned)threads + 1u) != 0 ||
        pthread_create(&commit_thread, NULL, commit_main, &queue) != 0) {
        fail("initialize group commit");
    }
    for (uint64_t index = 0; index < threads; ++index) {
        producers[index].queue = &queue;
        producers[index].start = &start;
        producers[index].sequence = &sequence;
        producers[index].operations = operations / threads +
            (index < operations % threads ? 1u : 0u);
        if (pthread_create(&producer_threads[index], NULL, producer_main,
                           &producers[index]) != 0) {
            fail("create commit producer");
        }
    }
    started = now_ns();
    wait_barrier(&start);
    for (uint64_t index = 0; index < threads; ++index) {
        if (pthread_join(producer_threads[index], NULL) != 0) {
            fail("join commit producer");
        }
    }
    elapsed = now_ns() - started;
    if (atomic_load(&sequence) != operations ||
        pthread_mutex_lock(&queue.mutex) != 0) {
        fail("finish group commit");
    }
    queue.stopping = true;
    if (pthread_cond_signal(&queue.available) != 0 ||
        pthread_mutex_unlock(&queue.mutex) != 0 ||
        pthread_join(commit_thread, NULL) != 0) {
        fail("stop group commit");
    }
    if (close(queue.fd) != 0) {
        fail("close concurrent journal");
    }
    verify_journal(path, operations);
    printf("NATIVE name=group-commit-%s operations=%" PRIu64
           " threads=%" PRIu64 " commits=%" PRIu64
           " max-batch=%" PRIu64 " elapsed-ns=%" PRIu64
           " ops/s=%.2f\n",
           filesystem ? "syncfs" : "fdatasync", operations, threads,
           queue.commits, queue.max_batch, elapsed,
           (double)operations * 1000000000.0 / (double)elapsed);
    pthread_barrier_destroy(&start);
    pthread_cond_destroy(&queue.completed);
    pthread_cond_destroy(&queue.available);
    pthread_mutex_destroy(&queue.mutex);
    free(producers);
    free(producer_threads);
    free(queue.batch);
}

static uint64_t parse_operations(const char *text)
{
    char *end = NULL;
    unsigned long long value;

    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0u ||
        value > SIZE_MAX / JOURNAL_RECORD_BYTES) {
        fputs("operations must be a positive allocation-sized integer\n",
              stderr);
        exit(EXIT_FAILURE);
    }
    return (uint64_t)value;
}

int main(int argc, char **argv)
{
    struct stat directory_state;
    uint8_t direct[DIRECT_BYTES];
    uint8_t *records;
    uint64_t operations;
    uint64_t batch;
    uint64_t max_threads;

    if (argc != 4) {
        fprintf(stderr, "usage: %s DIRECTORY OPERATIONS MAX_THREADS\n",
                argv[0]);
        return EXIT_FAILURE;
    }
    operations = parse_operations(argv[2]);
    max_threads = parse_operations(argv[3]);
    if (max_threads > operations || max_threads > UINT_MAX - 1u) {
        fputs("MAX_THREADS must fit the operation and pthread counts\n",
              stderr);
        return EXIT_FAILURE;
    }
    if (stat(argv[1], &directory_state) != 0 ||
        !S_ISDIR(directory_state.st_mode)) {
        fputs("benchmark directory does not exist\n", stderr);
        return EXIT_FAILURE;
    }
    records = malloc((size_t)operations * JOURNAL_RECORD_BYTES);
    if (records == NULL) {
        fail("allocate journal records");
    }
    for (size_t index = 0; index < sizeof(direct); ++index) {
        direct[index] = (uint8_t)(index * 37u + 11u);
    }
    for (uint64_t index = 0; index < operations; ++index) {
        uint8_t *record = records + index * JOURNAL_RECORD_BYTES;

        fill_record(record, index);
    }

    benchmark_direct(argv[1], operations, direct);
    for (batch = 1u; batch < operations; batch <<= 1u) {
        benchmark_journal(argv[1], operations, batch, records, false);
        if (batch > UINT64_MAX / 2u) {
            break;
        }
    }
    if (batch != operations) {
        benchmark_journal(argv[1], operations, operations, records, false);
    } else {
        benchmark_journal(argv[1], operations, batch, records, false);
    }
    for (batch = 1u; batch < max_threads; batch <<= 1u) {
        benchmark_journal(argv[1], operations, batch, records, true);
    }
    benchmark_journal(argv[1], operations, max_threads, records, true);
    for (batch = 1u; batch < max_threads; batch <<= 1u) {
        benchmark_group_commit(argv[1], operations, batch, false);
    }
    benchmark_group_commit(argv[1], operations, max_threads, false);
    benchmark_group_commit(argv[1], operations, max_threads, true);
    free(records);
    puts("ASTRA NATIVE HOSTFS DURABILITY PASS");
    return EXIT_SUCCESS;
}
