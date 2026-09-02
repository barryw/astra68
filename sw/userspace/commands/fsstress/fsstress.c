#define _POSIX_C_SOURCE 200809L

#include <astra/program.h>
#include <astra/divide.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

ASTRA_PROGRAM("fsstress", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

enum { OP_WRITE, OP_READ, OP_APPEND, OP_TRUNCATE, OP_RENAME, OP_MOVE,
       OP_DELETE, OP_SYNC, OP_COUNT };
enum { FAIL_OPERATION, FAIL_VERIFY_READ, FAIL_VERIFY_SYNC,
       FAIL_VERIFY_OPENDIR, FAIL_VERIFY_READDIR, FAIL_VERIFY_DIRCOUNT,
       FAIL_VERIFY_CLOSEDIR, FAIL_VERIFY_DELETE, FAIL_VERIFY_RMDIR,
       FAIL_PHASE_COUNT };
#define LATENCY_BUCKETS 44u
#define LATENCY_SAMPLE_INTERVAL 16u

typedef struct FileModel {
    uint8_t *bytes;
    uint32_t size;
    uint32_t capacity;
    uint8_t exists;
    uint8_t directory;
    uint8_t name;
} FileModel;

typedef struct Worker {
    const char *root;
    uint32_t index;
    uint32_t files;
    uint32_t max_bytes;
    uint32_t random;
    uint32_t operation;
    uint64_t operations;
    char *path;
    size_t path_capacity;
    char *file_paths;
    uint8_t *buffer;
    FileModel *model;
} Worker;

typedef struct Report {
    uint64_t operation_count[OP_COUNT];
    uint64_t latency[LATENCY_BUCKETS];
    uint64_t bytes_read;
    uint64_t bytes_written;
    uint64_t latency_max;
    uint64_t failed_operation;
    uint64_t operation_started;
    uint64_t operation_finished;
    uint64_t write_total_ns;
    uint64_t write_open_ns;
    uint64_t write_io_ns;
    uint64_t write_close_ns;
    uint32_t worker;
    uint32_t error;
    uint32_t failed_kind;
    uint32_t status;
} Report;

_Static_assert(sizeof(Report) <= 512u, "report must fit one atomic pipe write");

static const char *const operation_name[OP_COUNT] = {
    "write", "read", "append", "truncate", "rename", "move", "delete",
    "sync"
};

static const char *const failure_phase_name[FAIL_PHASE_COUNT] = {
    "operation", "verify-read", "verify-sync", "verify-opendir",
    "verify-readdir", "verify-dircount", "verify-closedir",
    "verify-delete", "verify-rmdir"
};

static uint64_t
now_ns(void)
{
    struct timespec now;

    return clock_gettime(CLOCK_MONOTONIC, &now) == 0 ?
        (uint64_t)now.tv_sec * UINT64_C(1000000000) +
            (uint32_t)now.tv_nsec : 0u;
}

static uint64_t
add_saturated(uint64_t left, uint64_t right)
{
    return right > UINT64_MAX - left ? UINT64_MAX : left + right;
}

static uint32_t
random_next(uint32_t *state)
{
    uint32_t value = *state;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static uint32_t
random_size(Worker *worker)
{
    return worker->max_bytes == UINT32_MAX ? random_next(&worker->random) :
        random_next(&worker->random) % (worker->max_bytes + 1u);
}

static int
model_reserve(FileModel *model, uint32_t size)
{
    uint8_t *bytes;
    uint32_t capacity;

    if (size <= model->capacity)
        return 0;
    capacity = model->capacity == 0u ? 64u : model->capacity;
    while (capacity < size) {
        uint32_t grown = capacity <= UINT32_MAX / 2u ? capacity * 2u : size;

        if (grown < capacity)
            return -1;
        capacity = grown;
    }
    bytes = realloc(model->bytes, capacity);
    if (bytes == NULL)
        return -1;
    model->bytes = bytes;
    model->capacity = capacity;
    return 0;
}

static int
model_resize(FileModel *model, uint32_t size)
{
    if (model_reserve(model, size) != 0)
        return -1;
    if (size > model->size)
        memset(model->bytes + model->size, 0, size - model->size);
    model->size = size;
    return 0;
}

static const char *
file_path(Worker *worker, uint32_t slot, uint8_t directory, uint8_t name)
{
    return worker->file_paths +
        ((size_t)slot * 4u + (size_t)directory * 2u + name) *
            worker->path_capacity;
}

static int
file_write_all(int descriptor, const uint8_t *bytes, uint32_t length)
{
    uint32_t done = 0u;

    while (done < length) {
        ssize_t moved = write(descriptor, bytes + done, length - done);

        if (moved <= 0)
            return -1;
        done += (uint32_t)moved;
    }
    return 0;
}

static void
make_bytes(uint8_t *bytes, uint32_t length, uint32_t token)
{
    uint32_t index = 0u;
    uint32_t value = token;

    while (length - index >= 8u) {
        bytes[index] = (uint8_t)value;
        bytes[index + 1u] = (uint8_t)(value + 131u);
        bytes[index + 2u] = (uint8_t)(value + 262u);
        bytes[index + 3u] = (uint8_t)(value + 393u);
        bytes[index + 4u] = (uint8_t)(value + 524u);
        bytes[index + 5u] = (uint8_t)(value + 655u);
        bytes[index + 6u] = (uint8_t)(value + 786u);
        bytes[index + 7u] = (uint8_t)(value + 917u);
        index += 8u;
        value += 1049u;
    }
    while (index < length) {
        bytes[index] = (uint8_t)(token + index * 131u + (index >> 3));
        ++index;
    }
}

static int
operation_write(Worker *worker, uint32_t slot, Report *report,
                uint64_t operation, int sample)
{
    FileModel *model = &worker->model[slot];
    const char *path = file_path(worker, slot, model->directory, model->name);
    uint32_t size = random_size(worker);
    uint64_t phase_started = 0u;
    uint64_t phase_finished;
    int descriptor = -1;

    if (path == NULL || model_reserve(model, size) != 0)
        return -1;
    model->size = size;
    make_bytes(model->bytes, size,
               worker->random ^ (uint32_t)operation ^ slot);
    if (sample)
        phase_started = now_ns();
    descriptor = open(path, O_CREAT | O_TRUNC | O_RDWR, 0666);
    if (sample) {
        phase_finished = now_ns();
        report->write_open_ns += phase_finished - phase_started;
        phase_started = phase_finished;
    }
    if (descriptor < 0)
        return -1;
    if (file_write_all(descriptor, model->bytes, size) != 0) {
        (void)close(descriptor);
        return -1;
    }
    if (sample) {
        phase_finished = now_ns();
        report->write_io_ns += phase_finished - phase_started;
        phase_started = phase_finished;
    }
    if (close(descriptor) != 0) {
        return -1;
    }
    if (sample) {
        phase_finished = now_ns();
        report->write_close_ns += phase_finished - phase_started;
    }
    model->exists = 1u;
    report->bytes_written = add_saturated(report->bytes_written, size);
    return 0;
}

static int
operation_read(Worker *worker, uint32_t slot, uint64_t *bytes_read)
{
    FileModel *model = &worker->model[slot];
    const char *path = file_path(worker, slot, model->directory, model->name);
    struct stat info;
    uint32_t done = 0u;
    int descriptor = -1;

    if (path == NULL || (descriptor = open(path, O_RDONLY)) < 0 ||
        fstat(descriptor, &info) != 0 || info.st_size != (off_t)model->size)
        goto fail;
    while (done < model->size) {
        ssize_t moved = read(descriptor, worker->buffer + done,
                             model->size - done);

        if (moved <= 0)
            goto fail;
        done += (uint32_t)moved;
    }
    if (read(descriptor, worker->buffer, 1u) != 0 ||
        (model->size != 0u &&
         memcmp(worker->buffer, model->bytes, model->size) != 0) ||
        close(descriptor) != 0)
        goto fail_closed;
    if (bytes_read != NULL)
        *bytes_read = add_saturated(*bytes_read, model->size);
    return 0;

fail:
    if (descriptor >= 0)
        (void)close(descriptor);
    return -1;
fail_closed:
    return -1;
}

static int
operation_append(Worker *worker, uint32_t slot, Report *report,
                 uint64_t operation)
{
    FileModel *model = &worker->model[slot];
    const char *path = file_path(worker, slot, model->directory, model->name);
    uint32_t remaining = worker->max_bytes - model->size;
    uint32_t added = remaining == 0u ? 0u : remaining == UINT32_MAX ?
        random_next(&worker->random) :
        random_next(&worker->random) % (remaining + 1u);
    uint32_t old_size = model->size;
    int descriptor = -1;

    if (path == NULL || model_reserve(model, old_size + added) != 0)
        return -1;
    make_bytes(model->bytes + old_size, added,
               worker->random ^ (uint32_t)operation ^ slot);
    model->size = old_size + added;
    descriptor = open(path, O_WRONLY | O_APPEND);
    if (descriptor < 0 ||
        file_write_all(descriptor, model->bytes + old_size, added) != 0 ||
        close(descriptor) != 0) {
        if (descriptor >= 0)
            (void)close(descriptor);
        model->size = old_size;
        return -1;
    }
    report->bytes_written = add_saturated(report->bytes_written, added);
    return 0;
}

static int
operation_truncate(Worker *worker, uint32_t slot)
{
    FileModel *model = &worker->model[slot];
    const char *path = file_path(worker, slot, model->directory, model->name);
    uint32_t size = random_size(worker);
    uint32_t old_size = model->size;
    int descriptor;

    if (path == NULL || model_resize(model, size) != 0)
        return -1;
    descriptor = open(path, O_RDWR);
    if (descriptor < 0 || ftruncate(descriptor, size) != 0 ||
        close(descriptor) != 0) {
        if (descriptor >= 0)
            (void)close(descriptor);
        model->size = old_size;
        return -1;
    }
    return 0;
}

static int
operation_rename(Worker *worker, uint32_t slot, int move)
{
    FileModel *model = &worker->model[slot];
    uint8_t new_directory = move ? (uint8_t)!model->directory :
                                   model->directory;
    uint8_t new_name = move ? model->name : (uint8_t)!model->name;
    const char *old_path = file_path(worker, slot, model->directory,
                                     model->name);
    const char *new_path;

    if (old_path == NULL)
        return -1;
    new_path = file_path(worker, slot, new_directory, new_name);
    if (new_path == NULL || rename(old_path, new_path) != 0) {
        return -1;
    }
    model->directory = new_directory;
    model->name = new_name;
    return 0;
}

static int
operation_delete(Worker *worker, uint32_t slot)
{
    FileModel *model = &worker->model[slot];
    const char *path = file_path(worker, slot, model->directory, model->name);

    if (path == NULL || unlink(path) != 0)
        return -1;
    free(model->bytes);
    memset(model, 0, sizeof(*model));
    return 0;
}

static int
operation_sync(Worker *worker, uint32_t slot)
{
    FileModel *model = &worker->model[slot];
    const char *path = file_path(worker, slot, model->directory, model->name);
    struct stat info;
    int descriptor = -1;

    if (path == NULL || (descriptor = open(path, O_RDWR)) < 0 ||
        fstat(descriptor, &info) != 0 || info.st_size != (off_t)model->size ||
        fsync(descriptor) != 0 || close(descriptor) != 0) {
        if (descriptor >= 0)
            (void)close(descriptor);
        return -1;
    }
    return 0;
}

static void
record_latency(Report *report, uint64_t elapsed)
{
    uint32_t bucket = 0u;

    while (bucket + 1u < LATENCY_BUCKETS && elapsed > (UINT64_C(1) << bucket))
        ++bucket;
    ++report->latency[bucket];
    if (elapsed > report->latency_max)
        report->latency_max = elapsed;
}

static int
directory_entries(const char *path, uint32_t *failure_phase)
{
    DIR *directory = opendir(path);
    struct dirent *entry;
    int count = 0;

    if (directory == NULL) {
        *failure_phase = FAIL_VERIFY_OPENDIR;
        return -1;
    }
    errno = 0;
    while ((entry = readdir(directory)) != NULL)
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            ++count;
    if (errno != 0) {
        *failure_phase = FAIL_VERIFY_READDIR;
        (void)closedir(directory);
        return -1;
    }
    if (closedir(directory) != 0) {
        *failure_phase = FAIL_VERIFY_CLOSEDIR;
        return -1;
    }
    return count;
}

static int
verify_and_clean(Worker *worker, Report *report)
{
    uint32_t expected[2] = {0u, 0u};
    char *directory = malloc(worker->path_capacity);

    if (directory == NULL)
        return -1;
    for (uint32_t slot = 0u; slot < worker->files; ++slot) {
        FileModel *model = &worker->model[slot];

        if (!model->exists)
            continue;
        ++expected[model->directory];
        if (operation_read(worker, slot, NULL) != 0) {
            report->failed_kind = OP_COUNT | (FAIL_VERIFY_READ << 16);
            free(directory);
            return -1;
        }
        if (operation_sync(worker, slot) != 0) {
            report->failed_kind = OP_COUNT | (FAIL_VERIFY_SYNC << 16);
            free(directory);
            return -1;
        }
    }
    for (uint32_t which = 0u; which < 2u; ++which) {
        uint32_t failure_phase = FAIL_VERIFY_DIRCOUNT;
        int length = snprintf(directory, worker->path_capacity,
                              "%s/w%" PRIu32 "/%c", worker->root,
                              worker->index, which ? 'b' : 'a');
        int actual = length < 0 || (size_t)length >= worker->path_capacity ?
            -1 : directory_entries(directory, &failure_phase);

        if (actual != (int)expected[which]) {
            report->failed_kind = OP_COUNT | (failure_phase << 16);
            free(directory);
            return -1;
        }
    }
    for (uint32_t slot = 0u; slot < worker->files; ++slot)
        if (worker->model[slot].exists &&
            operation_delete(worker, slot) != 0) {
            report->failed_kind = OP_COUNT | (FAIL_VERIFY_DELETE << 16);
            free(directory);
            return -1;
        }
    for (uint32_t which = 0u; which < 2u; ++which) {
        (void)snprintf(directory, worker->path_capacity,
                       "%s/w%" PRIu32 "/%c", worker->root, worker->index,
                       which ? 'b' : 'a');
        if (rmdir(directory) != 0) {
            report->failed_kind = OP_COUNT | (FAIL_VERIFY_RMDIR << 16);
            free(directory);
            return -1;
        }
    }
    (void)snprintf(directory, worker->path_capacity, "%s/w%" PRIu32,
                   worker->root, worker->index);
    if (rmdir(directory) != 0) {
        report->failed_kind = OP_COUNT | (FAIL_VERIFY_RMDIR << 16);
        free(directory);
        return -1;
    }
    free(directory);
    return 0;
}

static void
run_worker(Worker *worker, Report *report)
{
    memset(report, 0, sizeof(*report));
    report->worker = worker->index;
    worker->model = calloc(worker->files, sizeof(*worker->model));
    worker->buffer = malloc(worker->max_bytes == 0u ? 1u : worker->max_bytes);
    worker->path_capacity = strlen(worker->root) + 64u;
    worker->path = malloc(worker->path_capacity);
    worker->file_paths = worker->files > SIZE_MAX / 4u /
            worker->path_capacity ? NULL :
        malloc((size_t)worker->files * 4u * worker->path_capacity);
    if (worker->model == NULL || worker->buffer == NULL || worker->path == NULL ||
        worker->file_paths == NULL)
        goto failed;
    for (uint32_t slot = 0u; slot < worker->files; ++slot)
        for (uint32_t variant = 0u; variant < 4u; ++variant) {
            char *path = worker->file_paths +
                ((size_t)slot * 4u + variant) * worker->path_capacity;
            int length = snprintf(
                path, worker->path_capacity, "%s/w%" PRIu32 "/%c/f%" PRIu32
                "%c", worker->root, worker->index,
                (variant & 2u) != 0u ? 'b' : 'a', slot,
                (variant & 1u) != 0u ? 'y' : 'x');

            if (length < 0 || (size_t)length >= worker->path_capacity)
                goto failed;
        }

    report->operation_started = now_ns();
    for (uint64_t operation = 0u; operation < worker->operations; ++operation) {
        uint32_t slot = random_next(&worker->random) % worker->files;
        uint32_t kind = worker->operation < OP_COUNT ? worker->operation :
                        random_next(&worker->random) % OP_COUNT;
        uint64_t started = 0u;
        int sample_latency =
            operation % LATENCY_SAMPLE_INTERVAL == 0u;
        int result;

        if (!worker->model[slot].exists)
            kind = OP_WRITE;
        if (sample_latency)
            started = now_ns();
        switch (kind) {
        case OP_WRITE:
            result = operation_write(worker, slot, report, operation,
                                     sample_latency);
            break;
        case OP_READ:
            result = operation_read(worker, slot, &report->bytes_read);
            break;
        case OP_APPEND:
            result = operation_append(worker, slot, report, operation);
            break;
        case OP_TRUNCATE:
            result = operation_truncate(worker, slot);
            break;
        case OP_RENAME:
            result = operation_rename(worker, slot, 0);
            break;
        case OP_MOVE:
            result = operation_rename(worker, slot, 1);
            break;
        case OP_DELETE:
            result = operation_delete(worker, slot);
            break;
        default:
            result = operation_sync(worker, slot);
            break;
        }
        if (sample_latency) {
            uint64_t latency = now_ns() - started;

            record_latency(report, latency);
            if (kind == OP_WRITE)
                report->write_total_ns += latency;
        }
        ++report->operation_count[kind];
        if (result != 0) {
            report->failed_operation = operation;
            report->failed_kind = kind;
            report->error = (uint32_t)errno;
            report->operation_finished = now_ns();
            goto failed;
        }
    }
    report->operation_finished = now_ns();
    report->failed_kind = OP_COUNT;
    if (verify_and_clean(worker, report) != 0) {
        report->failed_operation = worker->operations;
        report->error = (uint32_t)errno;
        goto failed;
    }
    for (uint32_t slot = 0u; slot < worker->files; ++slot)
        free(worker->model[slot].bytes);
    free(worker->model);
    free(worker->buffer);
    free(worker->path);
    free(worker->file_paths);
    return;

failed:
    report->status = 1u;
}

static int
number(const char *text, uint64_t *value)
{
    char *end;
    unsigned long long parsed;

    if (text == NULL || text[0] == '\0' || text[0] == '-')
        return 0;
    errno = 0;
    parsed = strtoull(text, &end, 0);
    if (errno != 0 || *end != '\0')
        return 0;
    *value = parsed;
    return 1;
}

static int
operation(const char *text, uint32_t *kind)
{
    for (uint32_t index = 0u; index < OP_COUNT; ++index) {
        if (strcmp(text, operation_name[index]) == 0) {
            *kind = index;
            return 1;
        }
    }
    return 0;
}

static uint64_t
percentile(const uint64_t buckets[LATENCY_BUCKETS], uint64_t count,
           uint32_t percent)
{
    uint64_t target = count / 100u * percent +
        ((count % 100u) * percent + 99u) / 100u;
    uint64_t seen = 0u;

    for (uint32_t bucket = 0u; bucket < LATENCY_BUCKETS; ++bucket) {
        seen += buckets[bucket];
        if (seen >= target)
            return UINT64_C(1) << bucket;
    }
    return UINT64_C(1) << (LATENCY_BUCKETS - 1u);
}

static int
make_directories(const char *root, uint32_t workers)
{
    size_t capacity = strlen(root) + 64u;
    char *path = malloc(capacity);

    if (path == NULL || mkdir(root, 0777) != 0)
        goto fail;
    for (uint32_t worker = 0u; worker < workers; ++worker) {
        (void)snprintf(path, capacity, "%s/w%" PRIu32, root, worker);
        if (mkdir(path, 0777) != 0)
            goto fail;
        for (uint32_t which = 0u; which < 2u; ++which) {
            (void)snprintf(path, capacity, "%s/w%" PRIu32 "/%c", root,
                           worker, which ? 'b' : 'a');
            if (mkdir(path, 0777) != 0)
                goto fail;
        }
    }
    free(path);
    return 0;
fail:
    free(path);
    return -1;
}

static void
usage(void)
{
    fprintf(stderr, "usage: fsstress [-n operations] [-s seed] [-w workers] "
                    "[-f files] [-b max-bytes] [-o operation] [path]\n");
}

int
main(int argc, char **argv)
{
    uint64_t operations = 1000u, parsed;
    uint32_t seed = UINT32_C(0x68a57a31), workers = 4u, files = 16u;
    uint32_t max_bytes = 8192u;
    uint32_t selected_operation = OP_COUNT;
    const char *base = "WORK:fsstress";
    char *root;
    pid_t *children;
    Report aggregate = {0};
    uint64_t started, elapsed, operation_started = UINT64_MAX;
    uint64_t operation_finished = 0u, operation_elapsed, total_operations;
    uint64_t latency_samples;
    int reports[2];
    uint32_t launched = 0u;
    int failed = 0;
    int path_seen = 0;

    for (int at = 1; at < argc; ++at) {
        uint64_t *target = NULL;

        if (strcmp(argv[at], "-o") == 0) {
            if (++at == argc || !operation(argv[at], &selected_operation)) {
                usage();
                return 2;
            }
            continue;
        } else if (strcmp(argv[at], "-n") == 0) target = &operations;
        else if (strcmp(argv[at], "-s") == 0) target = &parsed;
        else if (strcmp(argv[at], "-w") == 0) target = &parsed;
        else if (strcmp(argv[at], "-f") == 0) target = &parsed;
        else if (strcmp(argv[at], "-b") == 0) target = &parsed;
        else if (argv[at][0] == '-' || path_seen) {
            usage();
            return 2;
        } else {
            base = argv[at];
            path_seen = 1;
            continue;
        }
        if (++at == argc || !number(argv[at], target)) {
            usage();
            return 2;
        }
        if (target == &parsed) {
            if (parsed == 0u || parsed > UINT32_MAX) {
                usage();
                return 2;
            }
            if (strcmp(argv[at - 1], "-s") == 0) seed = (uint32_t)parsed;
            else if (strcmp(argv[at - 1], "-w") == 0) workers = (uint32_t)parsed;
            else if (strcmp(argv[at - 1], "-f") == 0) files = (uint32_t)parsed;
            else max_bytes = (uint32_t)parsed;
        }
    }
    if (operations == 0u || seed == 0u ||
        operations > UINT64_MAX / workers ||
        workers > SIZE_MAX / sizeof(pid_t)) {
        usage();
        return 2;
    }
    root = malloc(strlen(base) + 10u);
    children = malloc((size_t)workers * sizeof(*children));
    if (root == NULL || children == NULL || pipe(reports) != 0)
        return 3;
    (void)sprintf(root, "%s-%08" PRIx32, base, seed);
    if (make_directories(root, workers) != 0) {
        fprintf(stderr, "fsstress: cannot create %s: %s\n", root,
                strerror(errno));
        return 4;
    }
    printf("fsstress: seed=0x%08" PRIx32 " workers=%" PRIu32
           " operations=%" PRIu64 " files=%" PRIu32 " max-bytes=%" PRIu32
           " root=%s\n", seed, workers, operations, files, max_bytes, root);
    (void)fflush(stdout);
    started = now_ns();
    for (uint32_t worker = 0u; worker < workers; ++worker) {
        pid_t child = fork();

        if (child < 0) {
            fprintf(stderr, "fsstress: fork worker=%" PRIu32 ": %s\n",
                    worker, strerror(errno));
            failed = 1;
            break;
        }
        if (child == 0) {
            Worker state = {
                .root = root,
                .index = worker,
                .files = files,
                .max_bytes = max_bytes,
                .random = seed ^
                    (UINT32_C(0x9e3779b9) * (worker + 1u)),
                .operation = selected_operation,
                .operations = operations,
            };
            Report report;

            (void)close(reports[0]);
            run_worker(&state, &report);
            if (write(reports[1], &report, sizeof(report)) !=
                    (ssize_t)sizeof(report))
                report.status = 1u;
            (void)close(reports[1]);
            _exit((int)report.status);
        }
        children[launched++] = child;
    }
    (void)close(reports[1]);
    for (uint32_t index = 0u; index < launched; ++index) {
        Report report;
        size_t received = 0u;

        while (received < sizeof(report)) {
            ssize_t moved = read(reports[0], (uint8_t *)&report + received,
                                 sizeof(report) - received);
            if (moved <= 0) {
                fprintf(stderr, "fsstress: report worker=%" PRIu32
                        ": %s\n", index,
                        moved < 0 ? strerror(errno) : "worker exited early");
                failed = 1;
                break;
            }
            received += (size_t)moved;
        }
        if (received != sizeof(report))
            break;
        if (report.status != 0u) {
            fprintf(stderr, "fsstress: worker=%" PRIu32 " operation=%" PRIu64
                    " kind=%s phase=%s errno=%" PRIu32 " root retained\n",
                    report.worker, report.failed_operation,
                    (report.failed_kind & UINT32_C(0xffff)) < OP_COUNT ?
                        operation_name[report.failed_kind &
                                       UINT32_C(0xffff)] : "verify",
                    (report.failed_kind >> 16) < FAIL_PHASE_COUNT ?
                        failure_phase_name[report.failed_kind >> 16] :
                        "unknown",
                    report.error);
            failed = 1;
        }
        for (uint32_t kind = 0u; kind < OP_COUNT; ++kind)
            aggregate.operation_count[kind] += report.operation_count[kind];
        for (uint32_t bucket = 0u; bucket < LATENCY_BUCKETS; ++bucket)
            aggregate.latency[bucket] += report.latency[bucket];
        aggregate.bytes_read = add_saturated(aggregate.bytes_read,
                                              report.bytes_read);
        aggregate.bytes_written = add_saturated(aggregate.bytes_written,
                                                 report.bytes_written);
        aggregate.write_total_ns += report.write_total_ns;
        aggregate.write_open_ns += report.write_open_ns;
        aggregate.write_io_ns += report.write_io_ns;
        aggregate.write_close_ns += report.write_close_ns;
        if (report.latency_max > aggregate.latency_max)
            aggregate.latency_max = report.latency_max;
        if (report.operation_started != 0u &&
            report.operation_started < operation_started)
            operation_started = report.operation_started;
        if (report.operation_finished > operation_finished)
            operation_finished = report.operation_finished;
    }
    (void)close(reports[0]);
    for (uint32_t index = 0u; index < launched; ++index) {
        int status = 0;

        if (failed == 0 &&
            (waitpid(children[index], &status, 0) != children[index] ||
             !WIFEXITED(status) || WEXITSTATUS(status) != 0))
            failed = 1;
    }
    elapsed = now_ns() - started;
    operation_elapsed = operation_started != UINT64_MAX &&
        operation_finished >= operation_started ?
        operation_finished - operation_started : 0u;
    total_operations = operations * launched;
    latency_samples = ((operations + LATENCY_SAMPLE_INTERVAL - 1u) /
                       LATENCY_SAMPLE_INTERVAL) * launched;
    printf("fsstress: stress-elapsed-ns=%" PRIu64 "\n", elapsed);
    printf("fsstress: operation-elapsed-ns=%" PRIu64
           " ops-per-second=%" PRIu64 "\n", operation_elapsed,
           operation_elapsed == 0u ? 0u : astra_multiply_divide_u64(
               total_operations, UINT64_C(1000000000), operation_elapsed));
    printf("fsstress: io-bytes-per-second=%" PRIu64 " read=%" PRIu64
           " written=%" PRIu64 "\n",
           operation_elapsed == 0u ? 0u : astra_multiply_divide_u64(
               add_saturated(aggregate.bytes_read, aggregate.bytes_written),
               UINT64_C(1000000000), operation_elapsed),
           aggregate.bytes_read, aggregate.bytes_written);
    printf("fsstress: latency-ns samples=%" PRIu64 " p50<=%" PRIu64
           " p95<=%" PRIu64 " p99<=%" PRIu64 " max=%" PRIu64 "\n",
           latency_samples,
           percentile(aggregate.latency, latency_samples, 50u),
           percentile(aggregate.latency, latency_samples, 95u),
           percentile(aggregate.latency, latency_samples, 99u),
           aggregate.latency_max);
    if (aggregate.operation_count[OP_WRITE] != 0u) {
        uint64_t known = aggregate.write_open_ns + aggregate.write_io_ns +
                         aggregate.write_close_ns;

        printf("fsstress: write-sample-ns total=%" PRIu64
               " open=%" PRIu64 " io=%" PRIu64 " close=%" PRIu64
               " other=%" PRIu64 "\n",
               aggregate.write_total_ns, aggregate.write_open_ns,
               aggregate.write_io_ns, aggregate.write_close_ns,
               aggregate.write_total_ns > known ?
                   aggregate.write_total_ns - known : 0u);
    }
    printf("fsstress: ops");
    for (uint32_t kind = 0u; kind < OP_COUNT; ++kind)
        printf(" %s=%" PRIu64, operation_name[kind],
               aggregate.operation_count[kind]);
    printf("\n");
    if (!failed && rmdir(root) == 0)
        printf("ASTRA FSSTRESS PASS seed=0x%08" PRIx32 " operations=%" PRIu64
               "\n", seed, total_operations);
    else {
        fprintf(stderr, "ASTRA FSSTRESS FAIL seed=0x%08" PRIx32
                        " root=%s\n", seed, root);
        failed = 1;
    }
    (void)fflush(stdout);
    free(children);
    free(root);
    return failed ? 1 : 0;
}
