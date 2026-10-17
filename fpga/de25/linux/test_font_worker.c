#include "astra_font_convert.h"
#include "astra_font_worker.h"

#include <astra/afnt.h>
#include <astra/font_worker.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static char **target_command;

static uint32_t decode32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
           (uint32_t)bytes[2] << 8 | bytes[3];
}

static void encode32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void transfer(int fd, void *buffer, size_t size, int output)
{
    uint8_t *bytes = buffer;
    while (size != 0u) {
        ssize_t moved = output ? write(fd, bytes, size) : read(fd, bytes, size);
        assert(moved > 0);
        bytes += moved;
        size -= (size_t)moved;
    }
}

static void check(const uint8_t *source, uint32_t source_size, int valid)
{
    int request_pipe[2], reply_pipe[2], child_status;
    uint8_t request[ASTRA_FONT_WORKER_REQUEST_SIZE] = {0};
    uint8_t reply[ASTRA_FONT_WORKER_REPLY_SIZE];
    uint8_t *afnt = NULL;
    pid_t child;

    assert(pipe(request_pipe) == 0 && pipe(reply_pipe) == 0);
    child = fork();
    assert(child >= 0);
    if (child == 0) {
        close(request_pipe[1]);
        close(reply_pipe[0]);
        if (target_command != NULL) {
            assert(dup2(request_pipe[0], STDIN_FILENO) == STDIN_FILENO);
            assert(dup2(reply_pipe[1], STDOUT_FILENO) == STDOUT_FILENO);
            close(request_pipe[0]);
            close(reply_pipe[1]);
            execvp(target_command[0], target_command);
            _exit(127);
        }
        int status = astra_font_worker_run(request_pipe[0], reply_pipe[1]);
        close(request_pipe[0]);
        close(reply_pipe[1]);
        _exit(status);
    }
    close(request_pipe[0]);
    close(reply_pipe[1]);
    memcpy(request, ASTRA_FONT_WORKER_REQUEST_MAGIC, 4u);
    encode32(request + 4u, ASTRA_FONT_WORKER_VERSION);
    encode32(request + 8u, 0u);
    encode32(request + 12u, source_size);
    encode32(request + 16u, 8u);
    encode32(request + 20u, 7u);
    transfer(request_pipe[1], request, sizeof(request), 1);
    if (source != NULL) {
        transfer(request_pipe[1], "test.ttf", 8u, 1);
        transfer(request_pipe[1], "OFL-1.1", 7u, 1);
        transfer(request_pipe[1], (void *)source, source_size, 1);
    }
    close(request_pipe[1]);
    transfer(reply_pipe[0], reply, sizeof(reply), 0);
    assert(memcmp(reply, ASTRA_FONT_WORKER_REPLY_MAGIC, 4u) == 0);
    assert(decode32(reply + 4u) == ASTRA_FONT_WORKER_VERSION);
    assert((decode32(reply + 8u) == ASTRA_FONT_CONVERT_OK) == valid);
    if (valid) {
        uint32_t length = decode32(reply + 16u);
        AstraAfntView view;
        assert(decode32(reply + 12u) >= 1u && length > 128u &&
               length <= ASTRA_FONT_WORKER_OUTPUT_MAX);
        afnt = malloc(length);
        assert(afnt != NULL);
        transfer(reply_pipe[0], afnt, length, 0);
        assert(astra_afnt_validate(afnt, length, &view));
        assert(view.strike_count == 4u);
    } else assert(decode32(reply + 16u) == 0u);
    free(afnt);
    close(reply_pipe[0]);
    assert(waitpid(child, &child_status, 0) == child &&
           WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
}

int main(int argc, char **argv)
{
    const char *path = "../../../sw/userspace/graphics/fonts/atkinson-hyperlegible-next/AtkinsonHyperlegibleNext-Regular.ttf";
    FILE *file = fopen(path, "rb");
    long size;
    uint8_t *source;

    assert(argc >= 1);
    if (argc > 1) target_command = argv + 1;
    assert(file != NULL && fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    assert(size > 0 && size <= ASTRA_FONT_WORKER_SOURCE_MAX &&
           fseek(file, 0, SEEK_SET) == 0);
    source = malloc((size_t)size);
    assert(source != NULL &&
           fread(source, 1u, (size_t)size, file) == (size_t)size &&
           fclose(file) == 0);
    check(source, (uint32_t)size, 1);
    memset(source, 0, 16u);
    check(source, (uint32_t)size, 0);
    check(NULL, ASTRA_FONT_WORKER_SOURCE_MAX + 1u, 0);
    free(source);
    puts("versioned HPS font worker passed");
    return 0;
}
