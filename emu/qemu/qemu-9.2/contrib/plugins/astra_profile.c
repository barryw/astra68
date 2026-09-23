/* Exact interval profiles for the Astra MC68040 guest. */
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <glib.h>
#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef struct {
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t executions;
    uint64_t baseline_executions;
    uint64_t loads;
    uint64_t stores;
    uint64_t load_bytes;
    uint64_t store_bytes;
    struct qemu_plugin_scoreboard *exec_count;
    uint32_t insns;
    uint32_t bytes;
    uint8_t signature[16];
    uint8_t signature_size;
} Block;

typedef struct {
    uint64_t probe;
    uint64_t caller;
    uint64_t count;
} Caller;

typedef struct {
    uint64_t probe;
    uint32_t value;
    uint64_t count;
} ProbeValue;

static GHashTable *blocks;
static GHashTable *callers;
static GHashTable *values;
static GArray *probes;
static GMutex blocks_lock;
static GThread *control_thread;
static char *control_path;
static char *output_path;
static char interval[65] = "profile";
static uint64_t started_ns;
static int control_fd = -1;
static bool count_memory;
static bool active;
static bool stopping;
static struct qemu_plugin_register *stack_pointer;
static struct qemu_plugin_register *probe_register;
static char *probe_register_name;
static GByteArray *register_bytes;
static GByteArray *probe_register_bytes;
static GByteArray *stack_bytes;

static uint64_t executions(const Block *block)
{
    if (block->exec_count != NULL) {
        return qemu_plugin_u64_sum(qemu_plugin_scoreboard_u64(block->exec_count));
    }
    return __atomic_load_n(&block->executions, __ATOMIC_RELAXED);
}

static uint64_t interval_executions(const Block *block)
{
    uint64_t total = executions(block);
    return total >= block->baseline_executions ?
           total - block->baseline_executions : 0;
}

static void free_block(gpointer value)
{
    Block *block = value;
    if (block->exec_count != NULL) {
        qemu_plugin_scoreboard_free(block->exec_count);
    }
    g_free(block);
}

static uint64_t monotonic_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ULL + now.tv_nsec;
}

static guint block_hash(gconstpointer value)
{
    const Block *block = value;
    uint64_t mixed = block->vaddr ^ (block->paddr << 13) ^
                     (block->paddr >> 19) ^ block->insns;
    return (guint)(mixed ^ (mixed >> 32));
}

static gboolean block_equal(gconstpointer left, gconstpointer right)
{
    const Block *a = left;
    const Block *b = right;
    return a->vaddr == b->vaddr && a->paddr == b->paddr &&
           a->insns == b->insns;
}

static guint caller_hash(gconstpointer value)
{
    const Caller *caller = value;
    uint64_t mixed = caller->probe ^ (caller->caller << 17) ^
                     (caller->caller >> 11);
    return (guint)(mixed ^ (mixed >> 32));
}

static gboolean caller_equal(gconstpointer left, gconstpointer right)
{
    const Caller *a = left;
    const Caller *b = right;
    return a->probe == b->probe && a->caller == b->caller;
}

static guint value_hash(gconstpointer value)
{
    const ProbeValue *sample = value;
    uint64_t mixed = sample->probe ^ ((uint64_t)sample->value << 19);
    return (guint)(mixed ^ (mixed >> 32));
}

static gboolean value_equal(gconstpointer left, gconstpointer right)
{
    const ProbeValue *a = left;
    const ProbeValue *b = right;
    return a->probe == b->probe && a->value == b->value;
}

static int compare_blocks(gconstpointer left, gconstpointer right)
{
    const Block *a = *(Block *const *)left;
    const Block *b = *(Block *const *)right;
    uint64_t ai = interval_executions(a) * a->insns;
    uint64_t bi = interval_executions(b) * b->insns;
    if (ai != bi) {
        return ai < bi ? 1 : -1;
    }
    if (a->vaddr != b->vaddr) {
        return a->vaddr < b->vaddr ? -1 : 1;
    }
    return 0;
}

static int compare_callers(gconstpointer left, gconstpointer right)
{
    const Caller *a = *(Caller *const *)left;
    const Caller *b = *(Caller *const *)right;
    if (a->count != b->count) {
        return a->count < b->count ? 1 : -1;
    }
    if (a->probe != b->probe) {
        return a->probe < b->probe ? -1 : 1;
    }
    return a->caller < b->caller ? -1 : a->caller != b->caller;
}

static int compare_values(gconstpointer left, gconstpointer right)
{
    const ProbeValue *a = *(ProbeValue *const *)left;
    const ProbeValue *b = *(ProbeValue *const *)right;
    if (a->count != b->count) {
        return a->count < b->count ? 1 : -1;
    }
    if (a->probe != b->probe) {
        return a->probe < b->probe ? -1 : 1;
    }
    return a->value < b->value ? -1 : a->value != b->value;
}

static void snapshot_counts(void)
{
    GHashTableIter iterator;
    gpointer value;
    g_mutex_lock(&blocks_lock);
    g_hash_table_iter_init(&iterator, blocks);
    while (g_hash_table_iter_next(&iterator, NULL, &value)) {
        Block *block = value;
        block->baseline_executions = executions(block);
        __atomic_store_n(&block->loads, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&block->stores, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&block->load_bytes, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&block->store_bytes, 0, __ATOMIC_RELAXED);
    }
    g_hash_table_remove_all(callers);
    g_hash_table_remove_all(values);
    g_mutex_unlock(&blocks_lock);
}

static bool valid_label(const char *label)
{
    size_t length = strlen(label);
    if (length == 0 || length >= sizeof(interval)) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        char c = label[i];
        if (!(g_ascii_isalnum(c) || c == '-' || c == '_' || c == '.')) {
            return false;
        }
    }
    return true;
}

static bool dump_interval(void)
{
    FILE *output;
    GPtrArray *ordered = g_ptr_array_new();
    GPtrArray *ordered_callers = g_ptr_array_new();
    GPtrArray *ordered_values = g_ptr_array_new();
    GHashTableIter iterator;
    gpointer value;
    uint64_t ended_ns = monotonic_ns();

    g_mutex_lock(&blocks_lock);
    g_hash_table_iter_init(&iterator, blocks);
    while (g_hash_table_iter_next(&iterator, NULL, &value)) {
        Block *block = value;
        if (interval_executions(block) != 0) {
            g_ptr_array_add(ordered, block);
        }
    }
    g_ptr_array_sort(ordered, compare_blocks);
    g_hash_table_iter_init(&iterator, callers);
    while (g_hash_table_iter_next(&iterator, NULL, &value)) {
        g_ptr_array_add(ordered_callers, value);
    }
    g_ptr_array_sort(ordered_callers, compare_callers);
    g_hash_table_iter_init(&iterator, values);
    while (g_hash_table_iter_next(&iterator, NULL, &value)) {
        g_ptr_array_add(ordered_values, value);
    }
    g_ptr_array_sort(ordered_values, compare_values);
    output = fopen(output_path, "a");
    if (output == NULL) {
        g_mutex_unlock(&blocks_lock);
        g_ptr_array_free(ordered, TRUE);
        g_ptr_array_free(ordered_callers, TRUE);
        g_ptr_array_free(ordered_values, TRUE);
        return false;
    }
    fprintf(output, "interval\t%s\t%" PRIu64 "\t%" PRIu64 "\n",
            interval, started_ns, ended_ns);
    for (guint index = 0; index < ordered->len; ++index) {
        Block *block = g_ptr_array_index(ordered, index);
        fprintf(output,
                "block\t0x%08" PRIx64 "\t0x%08" PRIx64
                "\t%u\t%u\t%" PRIu64 "\t%" PRIu64
                "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t",
                block->vaddr, block->paddr, block->insns, block->bytes,
                interval_executions(block),
                __atomic_load_n(&block->loads, __ATOMIC_RELAXED),
                __atomic_load_n(&block->stores, __ATOMIC_RELAXED),
                __atomic_load_n(&block->load_bytes, __ATOMIC_RELAXED),
                __atomic_load_n(&block->store_bytes, __ATOMIC_RELAXED));
        for (uint8_t byte = 0; byte < block->signature_size; ++byte) {
            fprintf(output, "%02x", block->signature[byte]);
        }
        fputc('\n', output);
    }
    for (guint index = 0; index < ordered_callers->len; ++index) {
        Caller *caller = g_ptr_array_index(ordered_callers, index);
        fprintf(output, "caller\t0x%08" PRIx64 "\t0x%08" PRIx64
                        "\t%" PRIu64 "\n",
                caller->probe, caller->caller, caller->count);
    }
    for (guint index = 0; index < ordered_values->len; ++index) {
        ProbeValue *sample = g_ptr_array_index(ordered_values, index);
        fprintf(output, "value\t0x%08" PRIx64 "\t%s\t0x%08" PRIx32
                        "\t%" PRIu64 "\n",
                sample->probe, probe_register_name, sample->value,
                sample->count);
    }
    fputs("end\n", output);
    bool ok = fclose(output) == 0;
    g_mutex_unlock(&blocks_lock);
    g_ptr_array_free(ordered, TRUE);
    g_ptr_array_free(ordered_callers, TRUE);
    g_ptr_array_free(ordered_values, TRUE);
    return ok;
}

static uint32_t load_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static void probe_entry(unsigned int cpu_index, void *userdata)
{
    (void)cpu_index;
    if (!__atomic_load_n(&active, __ATOMIC_ACQUIRE)) {
        return;
    }
    uint64_t probe = (uintptr_t)userdata;
    if (stack_pointer != NULL) {
        g_byte_array_set_size(register_bytes, 0);
        if (qemu_plugin_read_register(stack_pointer, register_bytes) == 4 &&
            register_bytes->len == 4) {
            uint32_t stack = load_be32(register_bytes->data);
            g_byte_array_set_size(stack_bytes, 0);
            if (qemu_plugin_read_memory_vaddr(stack, stack_bytes, 4) &&
                stack_bytes->len == 4) {
                Caller key = {
                    .probe = probe,
                    .caller = load_be32(stack_bytes->data),
                };
                g_mutex_lock(&blocks_lock);
                Caller *caller = g_hash_table_lookup(callers, &key);
                if (caller == NULL) {
                    caller = g_new0(Caller, 1);
                    *caller = key;
                    g_hash_table_insert(callers, caller, caller);
                }
                ++caller->count;
                g_mutex_unlock(&blocks_lock);
            }
        }
    }
    if (probe_register != NULL) {
        g_byte_array_set_size(probe_register_bytes, 0);
        if (qemu_plugin_read_register(probe_register,
                                      probe_register_bytes) == 4 &&
            probe_register_bytes->len == 4) {
            ProbeValue key = {
                .probe = probe,
                .value = load_be32(probe_register_bytes->data),
            };
            g_mutex_lock(&blocks_lock);
            ProbeValue *sample = g_hash_table_lookup(values, &key);
            if (sample == NULL) {
                sample = g_new0(ProbeValue, 1);
                *sample = key;
                g_hash_table_insert(values, sample, sample);
            }
            ++sample->count;
            g_mutex_unlock(&blocks_lock);
        }
    }
}

static void vcpu_init(qemu_plugin_id_t id, unsigned int cpu_index)
{
    (void)id;
    (void)cpu_index;
    g_autoptr(GArray) registers = qemu_plugin_get_registers();
    for (guint index = 0; index < registers->len; ++index) {
        qemu_plugin_reg_descriptor *descriptor = &g_array_index(
            registers, qemu_plugin_reg_descriptor, index);
        if (g_strcmp0(descriptor->name, "sp") == 0 ||
            g_strcmp0(descriptor->name, "a7") == 0) {
            stack_pointer = descriptor->handle;
        }
        if (g_strcmp0(descriptor->name, probe_register_name) == 0)
            probe_register = descriptor->handle;
    }
    if (stack_pointer != NULL) {
        register_bytes = g_byte_array_new();
        stack_bytes = g_byte_array_new();
    }
    if (probe_register != NULL)
        probe_register_bytes = g_byte_array_new();
}

static void reply(int fd, const char *text)
{
    (void)write(fd, text, strlen(text));
}

static gpointer control_main(gpointer unused)
{
    (void)unused;
    while (!__atomic_load_n(&stopping, __ATOMIC_ACQUIRE)) {
        int client = accept(control_fd, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        char command[96];
        ssize_t count = read(client, command, sizeof(command) - 1);
        if (count <= 0) {
            close(client);
            continue;
        }
        command[count] = '\0';
        command[strcspn(command, "\r\n")] = '\0';
        if (g_str_has_prefix(command, "start ") && valid_label(command + 6)) {
            __atomic_store_n(&active, false, __ATOMIC_RELEASE);
            snapshot_counts();
            g_strlcpy(interval, command + 6, sizeof(interval));
            started_ns = monotonic_ns();
            __atomic_store_n(&active, true, __ATOMIC_RELEASE);
            reply(client, "OK\n");
        } else if (strcmp(command, "stop") == 0) {
            __atomic_store_n(&active, false, __ATOMIC_RELEASE);
            reply(client, dump_interval() ? "OK\n" : "ERROR output\n");
        } else if (strcmp(command, "status") == 0) {
            reply(client, __atomic_load_n(&active, __ATOMIC_ACQUIRE) ?
                          "active\n" : "idle\n");
        } else {
            reply(client, "ERROR command\n");
        }
        close(client);
    }
    return NULL;
}

static void memory_access(unsigned int cpu_index, qemu_plugin_meminfo_t info,
                          uint64_t vaddr, void *userdata)
{
    (void)cpu_index;
    (void)vaddr;
    if (!__atomic_load_n(&active, __ATOMIC_ACQUIRE)) {
        return;
    }
    Block *block = userdata;
    uint64_t bytes = 1ULL << qemu_plugin_mem_size_shift(info);
    if (qemu_plugin_mem_is_store(info)) {
        __atomic_fetch_add(&block->stores, 1, __ATOMIC_RELAXED);
        __atomic_fetch_add(&block->store_bytes, bytes, __ATOMIC_RELAXED);
    } else {
        __atomic_fetch_add(&block->loads, 1, __ATOMIC_RELAXED);
        __atomic_fetch_add(&block->load_bytes, bytes, __ATOMIC_RELAXED);
    }
}

static void translate(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    (void)id;
    size_t instruction_count = qemu_plugin_tb_n_insns(tb);
    struct qemu_plugin_insn *first = qemu_plugin_tb_get_insn(tb, 0);
    Block key = {
        .vaddr = qemu_plugin_tb_vaddr(tb),
        .paddr = (uintptr_t)qemu_plugin_insn_haddr(first),
        .insns = instruction_count,
    };
    Block *block;

    g_mutex_lock(&blocks_lock);
    block = g_hash_table_lookup(blocks, &key);
    if (block == NULL) {
        block = g_new0(Block, 1);
        *block = key;
        block->exec_count = qemu_plugin_scoreboard_new(sizeof(uint64_t));
        for (size_t index = 0; index < instruction_count; ++index) {
            struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, index);
            size_t size = qemu_plugin_insn_size(insn);
            block->bytes += size;
            if (block->signature_size < sizeof(block->signature)) {
                block->signature_size += qemu_plugin_insn_data(
                    insn, block->signature + block->signature_size,
                    sizeof(block->signature) - block->signature_size);
            }
        }
        g_hash_table_insert(blocks, block, block);
    }
    g_mutex_unlock(&blocks_lock);

    qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(
        tb, QEMU_PLUGIN_INLINE_ADD_U64,
        qemu_plugin_scoreboard_u64(block->exec_count), 1);
    for (guint index = 0; index < probes->len; ++index) {
        uint64_t probe = g_array_index(probes, uint64_t, index);
        if (key.vaddr == probe) {
            qemu_plugin_register_vcpu_tb_exec_cb(
                tb, probe_entry, QEMU_PLUGIN_CB_R_REGS,
                (void *)(uintptr_t)probe);
        }
    }
    if (count_memory) {
        for (size_t index = 0; index < instruction_count; ++index) {
            qemu_plugin_register_vcpu_mem_cb(
                qemu_plugin_tb_get_insn(tb, index), memory_access,
                QEMU_PLUGIN_CB_NO_REGS, QEMU_PLUGIN_MEM_RW, block);
        }
    }
}

static void plugin_exit(qemu_plugin_id_t id, void *userdata)
{
    (void)id;
    (void)userdata;
    if (__atomic_exchange_n(&active, false, __ATOMIC_ACQ_REL)) {
        (void)dump_interval();
    }
    __atomic_store_n(&stopping, true, __ATOMIC_RELEASE);
    if (control_fd >= 0) {
        shutdown(control_fd, SHUT_RDWR);
        close(control_fd);
        control_fd = -1;
    }
    if (control_thread != NULL) {
        g_thread_join(control_thread);
    }
    if (control_path != NULL) {
        unlink(control_path);
    }
    g_hash_table_destroy(blocks);
    g_hash_table_destroy(callers);
    g_hash_table_destroy(values);
    g_array_free(probes, TRUE);
    if (register_bytes != NULL) {
        g_byte_array_unref(register_bytes);
    }
    if (probe_register_bytes != NULL) {
        g_byte_array_unref(probe_register_bytes);
    }
    if (stack_bytes != NULL) {
        g_byte_array_unref(stack_bytes);
    }
    g_free(control_path);
    g_free(output_path);
    g_free(probe_register_name);
}

static bool open_control_socket(void)
{
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    if (strlen(control_path) >= sizeof(address.sun_path)) {
        return false;
    }
    g_strlcpy(address.sun_path, control_path, sizeof(address.sun_path));
    unlink(control_path);
    control_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (control_fd < 0 || bind(control_fd, (struct sockaddr *)&address,
                               sizeof(address)) < 0 || listen(control_fd, 4) < 0) {
        return false;
    }
    control_thread = g_thread_new("astra-profile", control_main, NULL);
    return control_thread != NULL;
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                            const qemu_info_t *info,
                                            int argc, char **argv)
{
    (void)info;
    bool autostart = false;
    probes = g_array_new(FALSE, FALSE, sizeof(uint64_t));
    for (int index = 0; index < argc; ++index) {
        g_auto(GStrv) option = g_strsplit(argv[index], "=", 2);
        if (g_strcmp0(option[0], "output") == 0 && option[1] != NULL) {
            output_path = g_strdup(option[1]);
        } else if (g_strcmp0(option[0], "control") == 0 && option[1] != NULL) {
            control_path = g_strdup(option[1]);
        } else if (g_strcmp0(option[0], "memory") == 0) {
            if (!qemu_plugin_bool_parse(option[0], option[1], &count_memory)) {
                return -1;
            }
        } else if (g_strcmp0(option[0], "autostart") == 0) {
            if (!qemu_plugin_bool_parse(option[0], option[1], &autostart)) {
                return -1;
            }
        } else if (g_strcmp0(option[0], "label") == 0 && option[1] != NULL &&
                   valid_label(option[1])) {
            g_strlcpy(interval, option[1], sizeof(interval));
        } else if (g_strcmp0(option[0], "probe") == 0 && option[1] != NULL) {
            char *end = NULL;
            errno = 0;
            uint64_t probe = g_ascii_strtoull(option[1], &end, 0);
            if (errno != 0 || end == option[1] || *end != '\0' ||
                probe > UINT32_MAX) {
                fprintf(stderr, "astra_profile: invalid probe: %s\n",
                        option[1]);
                return -1;
            }
            bool duplicate = false;
            for (guint probe_index = 0; probe_index < probes->len;
                 ++probe_index) {
                duplicate |= g_array_index(probes, uint64_t, probe_index) ==
                             probe;
            }
            if (!duplicate) {
                g_array_append_val(probes, probe);
            }
        } else if (g_strcmp0(option[0], "register") == 0 &&
                   option[1] != NULL && valid_label(option[1]) &&
                   probe_register_name == NULL) {
            probe_register_name = g_strdup(option[1]);
        } else {
            fprintf(stderr, "astra_profile: invalid option: %s\n", argv[index]);
            return -1;
        }
    }
    if (output_path == NULL || (control_path == NULL && !autostart)) {
        fputs("astra_profile: output and control or autostart are required\n",
              stderr);
        return -1;
    }
    blocks = g_hash_table_new_full(block_hash, block_equal, free_block, NULL);
    callers = g_hash_table_new_full(caller_hash, caller_equal, g_free, NULL);
    values = g_hash_table_new_full(value_hash, value_equal, g_free, NULL);
    if (control_path != NULL && !open_control_socket()) {
        fprintf(stderr, "astra_profile: cannot open %s: %s\n",
                control_path, g_strerror(errno));
        return -1;
    }
    if (autostart) {
        started_ns = monotonic_ns();
        __atomic_store_n(&active, true, __ATOMIC_RELEASE);
    }
    qemu_plugin_register_vcpu_tb_trans_cb(id, translate);
    qemu_plugin_register_vcpu_init_cb(id, vcpu_init);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
