#include <astraz/engine.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void put16(unsigned char *data, unsigned offset, unsigned value)
{
    data[offset] = (unsigned char)(value >> 8);
    data[offset + 1] = (unsigned char)value;
}

static void put32(unsigned char *data, unsigned offset, unsigned value)
{
    data[offset] = (unsigned char)(value >> 24);
    data[offset + 1] = (unsigned char)(value >> 16);
    data[offset + 2] = (unsigned char)(value >> 8);
    data[offset + 3] = (unsigned char)value;
}

static int write_file(const char *path, const unsigned char *data, size_t size)
{
    FILE *file = fopen(path, "wb");
    if (!file) return 0;
    int ok = fwrite(data, 1, size, file) == size;
    if (fclose(file) != 0) ok = 0;
    return ok;
}

static int check(const char *name, const char *path, const unsigned char *data,
                 size_t size, int status, unsigned vm, unsigned version,
                 unsigned container)
{
    AstraZStory story = {0};
    int actual = write_file(path, data, size) ?
        astraz_engine_probe(path, &story) : ASTRAZ_IO;
    if (actual != status || (status == ASTRAZ_OK &&
        (story.vm != vm || story.version != version ||
         story.container != container))) {
        fprintf(stderr, "%s: status=%d vm=%u version=%u container=%u\n",
                name, actual, story.vm, story.version, story.container);
        return 0;
    }
    return 1;
}

static int event(void *context, const AstraZEvent *item)
{
    (void)context;
    (void)item;
    return 0;
}

static uint16_t read_key(void *context, uint32_t timeout, int cursor)
{
    (void)context;
    (void)timeout;
    (void)cursor;
    return ASTRAZ_KEY_RETURN;
}

static uint16_t read_line(void *context, uint16_t *buffer, uint32_t max,
                          uint32_t timeout, uint32_t width, int continued)
{
    (void)context;
    (void)timeout;
    (void)width;
    (void)continued;
    if (max) buffer[0] = 0;
    return ASTRAZ_KEY_RETURN;
}

static int choose_file(void *context, int purpose, const char *suggested,
                       char *path, size_t capacity)
{
    (void)context;
    (void)purpose;
    (void)suggested;
    (void)path;
    (void)capacity;
    return 0;
}

int main(void)
{
    char path[] = "/work/astraz-probe-XXXXXX";
    int descriptor = mkstemp(path);
    if (descriptor < 0) return 2;
    close(descriptor);
    unsigned char zcode[256] = {0};
    put16(zcode, 0x08, 64);
    put16(zcode, 0x0a, 80);
    put16(zcode, 0x0c, 96);
    put16(zcode, 0x0e, 128);
    int ok = 1;
    for (unsigned version = 1; version <= 8; ++version) {
        zcode[0] = (unsigned char)version;
        ok &= check("bare Z", path, zcode, sizeof(zcode), ASTRAZ_OK,
                    ASTRAZ_VM_Z, version, ASTRAZ_CONTAINER_BARE);
    }
    unsigned char blorb[300] = {0};
    memcpy(blorb, "FORM", 4);
    put32(blorb, 4, sizeof(blorb) - 8);
    memcpy(blorb + 8, "IFRSRIdx", 8);
    put32(blorb, 16, 16);
    put32(blorb, 20, 1);
    memcpy(blorb + 24, "Exec", 4);
    put32(blorb, 32, 36);
    memcpy(blorb + 36, "ZCOD", 4);
    put32(blorb, 40, sizeof(zcode));
    memcpy(blorb + 44, zcode, sizeof(zcode));
    ok &= check("Z Blorb", path, blorb, sizeof(blorb), ASTRAZ_OK,
                ASTRAZ_VM_Z, 8, ASTRAZ_CONTAINER_BLORB);
    memcpy(blorb + 24, "Pict", 4);
    ok &= check("Blorb without executable", path, blorb, sizeof(blorb),
                ASTRAZ_INVALID, 0, 0, 0);
    memcpy(blorb + 24, "Exec", 4);
    unsigned char glulx[256] = {0};
    memcpy(glulx, "Glul", 4);
    put32(glulx, 4, 0x00030100);
    put32(glulx, 8, 256);
    put32(glulx, 12, 256);
    put32(glulx, 16, 256);
    put32(glulx, 20, 256);
    put32(glulx, 24, 36);
    ok &= check("bare Glulx", path, glulx, sizeof(glulx), ASTRAZ_OK,
                ASTRAZ_VM_GLULX, 0x00030100, ASTRAZ_CONTAINER_BARE);
    memcpy(blorb + 36, "GLUL", 4);
    memcpy(blorb + 44, glulx, sizeof(glulx));
    ok &= check("Glulx Blorb", path, blorb, sizeof(blorb), ASTRAZ_OK,
                ASTRAZ_VM_GLULX, 0x00030100, ASTRAZ_CONTAINER_BLORB);
    blorb[7] ^= 1u;
    ok &= check("wrong Blorb length", path, blorb, sizeof(blorb),
                ASTRAZ_INVALID, 0, 0, 0);
    ok &= check("truncated story", path, zcode, 63, ASTRAZ_INVALID, 0, 0, 0);
    zcode[0] = 0;
    ok &= check("unknown story", path, zcode, sizeof(zcode),
                ASTRAZ_UNSUPPORTED_VM, 0, 0, 0);
    zcode[0] = 6;
    AstraZHost host = {.width = 80, .height = 24, .cell_width = 1,
        .cell_height = 1, .event = event, .read_key = read_key,
        .read_line = read_line, .choose_file = choose_file,
        .capabilities = ASTRAZ_CAP_MOUSE};
    AstraZRun run = {.story_path = path, .host = &host};
    if (!write_file(path, zcode, sizeof(zcode)) ||
        astraz_engine_run(&run) != ASTRAZ_MISSING_CAPABILITY)
        ok = 0;
    host.capabilities = 0;
    host.width = 256;
    if (astraz_engine_run(&run) != ASTRAZ_INVALID) ok = 0;
    if (unlink(path) != 0) ok = 0;
    puts(ok ? "AstraZ engine contract PASS" : "AstraZ engine contract FAIL");
    return ok ? 0 : 1;
}
