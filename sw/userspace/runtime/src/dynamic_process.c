#include <astra/dynamic_process.h>
#include <astra/runtime.h>
#include <astra/syscall.h>

#include <stddef.h>
#include <stdint.h>
#if defined(ASTRA_DYNAMIC_PROCESS_TEST)
#include <stdlib.h>
#endif

typedef struct AstraDynamicEdge {
    uint32_t owner;
    uint32_t dependency;
} AstraDynamicEdge;

struct AstraDynamicProcess {
    AstraDynamicImage *images;
    const char **identities;
    void **tokens;
    AstraDynamicEdge *edges;
    uint32_t *lifecycle;
    uint32_t image_count;
    uint32_t image_capacity;
    uint32_t identity_capacity;
    uint32_t token_capacity;
    uint32_t edge_count;
    uint32_t edge_capacity;
    uint32_t lifecycle_count;
    AstraDynamicProcessFinish finish;
    void *context;
    uint8_t committed;
};

static void *process_callocate(size_t count, size_t size)
{
#if defined(ASTRA_DYNAMIC_PROCESS_TEST)
    return calloc(count, size);
#else
    return astra_runtime_callocate(count, size);
#endif
}

static void *process_reallocate(void *storage, size_t size)
{
#if defined(ASTRA_DYNAMIC_PROCESS_TEST)
    return realloc(storage, size);
#else
    return astra_runtime_reallocate(storage, size);
#endif
}

static void process_deallocate(void *storage)
{
#if defined(ASTRA_DYNAMIC_PROCESS_TEST)
    free(storage);
#else
    astra_runtime_deallocate(storage);
#endif
}

static int text_equal(const char *left, const char *right)
{
    if (left == NULL || right == NULL)
        return left == right;
    while (*left == *right) {
        if (*left == '\0')
            return 1;
        ++left;
        ++right;
    }
    return 0;
}

static int grow_array(void **storage, uint32_t *capacity, uint32_t needed,
                      size_t element_size)
{
    uint32_t next;
    void *replacement;

    if (needed <= *capacity)
        return 1;
    next = *capacity != 0u ? *capacity : 4u;
    while (next < needed) {
        if (next > UINT32_MAX / 2u)
            return 0;
        next *= 2u;
    }
    if ((size_t)next > SIZE_MAX / element_size)
        return 0;
    replacement = process_reallocate(*storage, (size_t)next * element_size);
    if (replacement == NULL)
        return 0;
    *storage = replacement;
    *capacity = next;
    return 1;
}

static int grow_images(AstraDynamicProcess *process, uint32_t needed)
{
    uint32_t old_identity_capacity = process->identity_capacity;

    if (needed <= process->image_capacity &&
        needed <= process->identity_capacity &&
        needed <= process->token_capacity)
        return 1;
    if (!grow_array((void **)&process->images, &process->image_capacity,
                    needed, sizeof(*process->images)) ||
        !grow_array((void **)&process->identities,
                    &process->identity_capacity, needed,
                    sizeof(*process->identities)) ||
        !grow_array((void **)&process->tokens, &process->token_capacity,
                    needed, sizeof(*process->tokens)))
        return 0;
    for (uint32_t index = old_identity_capacity;
         index < process->identity_capacity; ++index) {
        process->identities[index] = NULL;
        process->tokens[index] = NULL;
    }
    return 1;
}

static uint32_t find_identity(const AstraDynamicProcess *process,
                              const char *identity)
{
    for (uint32_t index = 1u; index < process->image_count; ++index)
        if (text_equal(process->identities[index], identity))
            return index;
    return UINT32_MAX;
}

static int append_edge(AstraDynamicProcess *process, uint32_t owner,
                       uint32_t dependency)
{
    for (uint32_t index = 0u; index < process->edge_count; ++index)
        if (process->edges[index].owner == owner &&
            process->edges[index].dependency == dependency)
            return 1;
    if (!grow_array((void **)&process->edges, &process->edge_capacity,
                    process->edge_count + 1u, sizeof(*process->edges)))
        return 0;
    process->edges[process->edge_count++] =
        (AstraDynamicEdge){owner, dependency};
    return 1;
}

static int lifecycle_build(AstraDynamicProcess *process)
{
    uint8_t *emitted;

    process->lifecycle = process_callocate(process->image_count,
                                            sizeof(*process->lifecycle));
    emitted = process_callocate(process->image_count, sizeof(*emitted));
    if (process->lifecycle == NULL || emitted == NULL) {
        process_deallocate(emitted);
        return 0;
    }
    while (process->lifecycle_count < process->image_count) {
        uint32_t selected = UINT32_MAX;

        for (uint32_t image = 0u; image < process->image_count; ++image) {
            int blocked = 0;

            if (emitted[image] != 0u)
                continue;
            for (uint32_t edge = 0u; edge < process->edge_count; ++edge)
                if (process->edges[edge].owner == image &&
                    emitted[process->edges[edge].dependency] == 0u) {
                    blocked = 1;
                    break;
                }
            if (!blocked) {
                selected = image;
                break;
            }
        }
        if (selected == UINT32_MAX) {
            process_deallocate(emitted);
            return 0;
        }
        emitted[selected] = 1u;
        process->lifecycle[process->lifecycle_count++] = selected;
    }
    process_deallocate(emitted);
    return 1;
}

static void rollback(AstraDynamicProcess *process)
{
    if (process == NULL || process->finish == NULL)
        return;
    for (uint32_t index = process->image_count; index > 1u; --index)
        if (process->tokens[index - 1u] != NULL) {
            (void)process->finish(process->context,
                                  process->tokens[index - 1u], 0);
            process->tokens[index - 1u] = NULL;
        }
}

uint32_t astra_dynamic_process_prepare(
    const AstraDynamicImage *program, AstraDynamicProcessOpen open_image,
    AstraDynamicProcessFinish finish_image, void *context,
    AstraDynamicProcess **out)
{
    AstraDynamicProcess *process;

    if (out != NULL)
        *out = NULL;
    if (program == NULL || program->elf_type != 2u || open_image == NULL ||
        finish_image == NULL || out == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    process = process_callocate(1u, sizeof(*process));
    if (process == NULL)
        return ASTRA_SYSCALL_OUT_OF_MEMORY;
    process->finish = finish_image;
    process->context = context;
    if (!grow_images(process, 1u)) {
        astra_dynamic_process_discard(process);
        return ASTRA_SYSCALL_OUT_OF_MEMORY;
    }
    process->images[0] = *program;
    process->image_count = 1u;

    for (uint32_t owner = 0u; owner < process->image_count; ++owner) {
        for (uint32_t needed = 0u;
             needed < process->images[owner].needed_count; ++needed) {
            AstraDynamicImage image;
            const char *identity;
            const char *soname;
            void *token = NULL;
            uint32_t dependency;
            uint32_t status;

            if (astra_dynamic_needed(&process->images[owner], needed,
                                     &identity) != ASTRA_DYNAMIC_OK) {
                astra_dynamic_process_discard(process);
                return ASTRA_SYSCALL_INVALID_ARGUMENT;
            }
            dependency = find_identity(process, identity);
            if (dependency == UINT32_MAX) {
                status = open_image(context, identity, &image, &token);
                if (status != ASTRA_SYSCALL_OK) {
                    astra_dynamic_process_discard(process);
                    return status;
                }
                if (token == NULL ||
                    astra_dynamic_soname(&image, &soname) !=
                        ASTRA_DYNAMIC_OK ||
                    !text_equal(identity, soname)) {
                    (void)finish_image(context, token, 0);
                    astra_dynamic_process_discard(process);
                    return ASTRA_SYSCALL_INVALID_ARGUMENT;
                }
                if (!grow_images(process, process->image_count + 1u)) {
                    (void)finish_image(context, token, 0);
                    astra_dynamic_process_discard(process);
                    return ASTRA_SYSCALL_OUT_OF_MEMORY;
                }
                dependency = process->image_count++;
                process->images[dependency] = image;
                process->identities[dependency] = soname;
                process->tokens[dependency] = token;
            }
            if (!append_edge(process, owner, dependency)) {
                astra_dynamic_process_discard(process);
                return ASTRA_SYSCALL_OUT_OF_MEMORY;
            }
        }
    }
    if (!lifecycle_build(process)) {
        astra_dynamic_process_discard(process);
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    *out = process;
    return ASTRA_SYSCALL_OK;
}

AstraDynamicStatus astra_dynamic_process_relocate(
    AstraDynamicProcess *process, AstraDynamicTlsLayout *layout,
    uint32_t *failure_image)
{
    AstraDynamicClosure closure;
    AstraDynamicStatus status;

    if (failure_image != NULL)
        *failure_image = UINT32_MAX;
    if (process == NULL || layout == NULL || failure_image == NULL ||
        process->committed != 0u)
        return ASTRA_DYNAMIC_INVALID_ARGUMENT;
    status = astra_dynamic_tls_layout(process->images, process->image_count,
                                      layout);
    if (status != ASTRA_DYNAMIC_OK)
        return status;
    closure.images = process->images;
    closure.count = process->image_count;
    for (uint32_t index = 0u; index < process->image_count; ++index) {
        status = astra_dynamic_relocate(&process->images[index],
                                        astra_dynamic_resolve_closure,
                                        &closure);
        if (status != ASTRA_DYNAMIC_OK) {
            *failure_image = index;
            return status;
        }
    }
    return ASTRA_DYNAMIC_OK;
}

uint32_t astra_dynamic_process_commit(AstraDynamicProcess *process)
{
    if (process == NULL || process->committed != 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    for (uint32_t index = 1u; index < process->image_count; ++index) {
        uint32_t status = process->finish(process->context,
                                          process->tokens[index], 1);

        if (status != ASTRA_SYSCALL_OK) {
            for (uint32_t rollback_index = process->image_count;
                 rollback_index > index; --rollback_index)
                if (process->tokens[rollback_index - 1u] != NULL) {
                    (void)process->finish(
                        process->context,
                        process->tokens[rollback_index - 1u], 0);
                    process->tokens[rollback_index - 1u] = NULL;
                }
            return status;
        }
        process->tokens[index] = NULL;
    }
    process->committed = 1u;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_dynamic_process_tls_initialize(
    const AstraDynamicProcess *process, const AstraDynamicTlsLayout *layout,
    void *destination, uint32_t capacity)
{
    if (process == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    return astra_dynamic_tls_initialize(
               process->images, process->image_count, layout,
               destination, capacity) == ASTRA_DYNAMIC_OK ?
        ASTRA_SYSCALL_OK : ASTRA_SYSCALL_INVALID_ARGUMENT;
}

void astra_dynamic_process_discard(AstraDynamicProcess *process)
{
    if (process == NULL)
        return;
    if (process->committed == 0u)
        rollback(process);
    process_deallocate(process->lifecycle);
    process_deallocate(process->edges);
    process_deallocate(process->tokens);
    process_deallocate(process->identities);
    process_deallocate(process->images);
    process_deallocate(process);
}

uint32_t astra_dynamic_process_image_count(const AstraDynamicProcess *process)
{
    return process != NULL ? process->image_count : 0u;
}

const AstraDynamicImage *astra_dynamic_process_image(
    const AstraDynamicProcess *process, uint32_t index)
{
    return process != NULL && index < process->image_count ?
        &process->images[index] : NULL;
}

const char *astra_dynamic_process_identity(
    const AstraDynamicProcess *process, uint32_t index)
{
    return process != NULL && index < process->image_count ?
        process->identities[index] : NULL;
}

uint32_t astra_dynamic_process_lifecycle_count(
    const AstraDynamicProcess *process)
{
    return process != NULL ? process->lifecycle_count : 0u;
}

uint32_t astra_dynamic_process_lifecycle_image(
    const AstraDynamicProcess *process, uint32_t index)
{
    return process != NULL && index < process->lifecycle_count ?
        process->lifecycle[index] : UINT32_MAX;
}
