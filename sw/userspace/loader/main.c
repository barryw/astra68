/** @file main.c @brief Production eager interpreter for Astra programs. */

#include <astra/dynamic_loader.h>
#include <astra/dynamic_process.h>
#include <astra/bundle.h>
#include <astra/bundle_internal.h>
#include <astra/endian.h>
#include <astra/library.h>
#include <astra/process.h>
#include <astra/runtime.h>
#include <astra/status.h>
#include <astra/syscall.h>
#include <astra/vfs_process.h>
#include <astra/vfs_library_source.h>
#include <astra/vfs_provider_index.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../runtime/src/stream_source.h"

#define ASTRA_PROGRAM_HEADER_ADDRESS ASTRA_EXECUTABLE_LINK_ADDRESS

enum LoaderFailure {
    LOADER_FAILURE_STARTUP = ASTRA_STATUS_PROGRAM_FIRST,
    LOADER_FAILURE_VFS,
    LOADER_FAILURE_DEPENDENCIES,
    LOADER_FAILURE_RELOCATION,
    LOADER_FAILURE_TLS_ALLOCATION,
    LOADER_FAILURE_TLS_INITIALIZATION,
    LOADER_FAILURE_LIBRARY_COMMIT,
    LOADER_FAILURE_PROCESS_COMMIT,
    LOADER_FAILURE_INITIALIZERS,
    LOADER_FAILURE_PROGRAM_STATUS
};


/* Loader-side refusals, logged with a failed dependency. */
#define LOADER_REFUSED_MAPPING   0x1001u
#define LOADER_REFUSED_DYNAMIC   0x1002u
#define LOADER_REFUSED_SONAME    0x1003u
#define LOADER_REFUSED_SPAN      0x1004u
#define LOADER_REFUSED_IDENTITY  0x1005u

static int loader_failure(const char *operation, uint32_t status,
                          enum LoaderFailure failure)
{
    (void)astra_log_failure(operation, status);
    return (int)failure;
}

typedef struct LoaderDependency {
    AstraVfsReadSource source;
    uint32_t load_handle;
} LoaderDependency;

static int identity_matches(const char *expected,
                            const AstraLibrary *identity,
                            const char *soname);

static uint32_t private_manifest_open(AstraBundleManifest *manifest)
{
    AstraVfsReadSource source = ASTRA_VFS_READ_SOURCE_INIT;
    char *text = NULL;
    uint32_t status = astra_process_vfs_source_open("/app/manifest", &source);

    if (status != ASTRA_VFS_OK) return status;
    if (source.length == 0u || source.length == UINT32_MAX) {
        status = ASTRA_VFS_ERR_LIMIT;
        goto done;
    }
    text = astra_runtime_allocate(source.length + 1u);
    if (text == NULL) {
        status = ASTRA_VFS_ERR_LIMIT;
        goto done;
    }
    for (uint32_t at = 0u; at < source.length;) {
        const uint8_t *bytes = NULL;
        uint32_t moved = 0u;

        status = astra_vfs_read_source_read_at(&source, at,
            source.length - at, &bytes, &moved);
        if (status != ASTRA_VFS_OK) goto done;
        if (bytes == NULL || moved == 0u) {
            status = ASTRA_VFS_ERR_IO;
            goto done;
        }
        memcpy(text + at, bytes, moved);
        at += moved;
    }
    text[source.length] = '\0';
    uint32_t line = 0u;
    status = astra_bundle_manifest_parse(text, source.length, manifest, &line);
    if (status != ASTRA_BUNDLE_OK || manifest->kind != ASTRA_BUNDLE_APPLICATION) {
        astra_bundle_manifest_destroy(manifest);
        status = ASTRA_VFS_ERR_INVALID;
    } else status = ASTRA_VFS_OK;
done:
    astra_runtime_deallocate(text);
    if (astra_vfs_read_source_close(&source) != ASTRA_VFS_OK &&
        status == ASTRA_VFS_OK) status = ASTRA_VFS_ERR_IO;
    return status;
}

static const AstraBundleLibrary *private_library(const AstraBundleManifest *manifest,
                                                  const char *identity)
{
    char name[ASTRA_LIBRARY_NAME_MAX];
    uint16_t abi;

    if (!astra_vfs_provider_identity_parse(
            identity, name, sizeof(name), &abi)) return NULL;
    return astra_bundle_private_provider(manifest, name, abi);
}

static int required_version_satisfied(const AstraBundleManifest *manifest,
                                      const char *identity,
                                      const AstraLibraryReference *reference)
{
    char name[ASTRA_LIBRARY_NAME_MAX];
    uint16_t abi;

    if (manifest == NULL || manifest->kind != ASTRA_BUNDLE_APPLICATION)
        return 1;
    if (!astra_vfs_provider_identity_parse(identity, name, sizeof(name), &abi))
        return 0;
    for (uint32_t at = 0u; at < manifest->require_count; ++at) {
        const AstraBundleLibrary *required = &manifest->requirements[at];

        if (strcmp(required->name, name) != 0 || required->abi != abi)
            continue;
        if (reference->major < required->version.major ||
            (reference->major == required->version.major &&
             reference->minor < required->version.minor) ||
            (reference->major == required->version.major &&
             reference->minor == required->version.minor &&
             reference->patch < required->version.patch))
            return 0;
    }
    return 1;
}

static uint32_t private_payload_source(const AstraBundleLibrary *library,
                                       const char *identity, char *path,
                                       AstraVfsReadSource *source,
                                       AstraLibraryReference *reference)
{
    const uint8_t *bytes = NULL;
    AstraLibrary record;
    uint32_t status;
    if (!astra_bundle_library_payload_path(
            "/app/", ASTRA_BUNDLE_APPLICATION, library,
            path, ASTRA_VFS_PATH_MAX))
        return ASTRA_VFS_ERR_LIMIT;
    status = astra_process_vfs_source_open(path, source);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_stream_read_exact(astra_vfs_read_source_read_at, source,
        source->length, ASTRA_LIBRARY_FILE_OFFSET, ASTRA_LIBRARY_SIZE,
        &bytes, NULL);
    if (status != ASTRA_SYSCALL_OK || bytes == NULL)
        return ASTRA_VFS_ERR_INVALID;
    memcpy(&record, bytes, sizeof(record));
    if (!identity_matches(identity, &record, identity) ||
        record.major != library->version.major ||
        record.minor != library->version.minor ||
        record.patch != library->version.patch ||
        record.abi_major != library->abi ||
        record.reserved_flags != 0u || record.reserved != 0u)
        return ASTRA_VFS_ERR_INVALID;
    *reference = (AstraLibraryReference){
        .size = ASTRA_LIBRARY_REFERENCE_SIZE,
        .build_id = record.build_id,
        .major = record.major,
        .minor = record.minor,
        .patch = record.patch,
        .abi_major = record.abi_major,
        .abi_minor = record.abi_minor,
    };
    memcpy(reference->name, record.name, sizeof(reference->name));
    return ASTRA_VFS_OK;
}

static int text_equal(const char *left, const char *right, uint32_t capacity)
{
    if (left == NULL || right == NULL)
        return 0;
    for (uint32_t index = 0u; index < capacity; ++index) {
        if (left[index] != right[index])
            return 0;
        if (left[index] == '\0')
            return 1;
    }
    return 0;
}

static int identity_matches(const char *expected,
                            const AstraLibrary *identity,
                            const char *soname)
{
    return expected != NULL && identity != NULL && soname != NULL &&
           identity->magic == ASTRA_LIBRARY_MAGIC &&
           identity->record_version == ASTRA_LIBRARY_RECORD_VERSION &&
           identity->header_size == ASTRA_LIBRARY_SIZE &&
           identity->target == ASTRA_LIBRARY_TARGET_M68040 &&
           text_equal(identity->name, expected, ASTRA_LIBRARY_NAME_MAX) &&
           text_equal(identity->name, soname, ASTRA_LIBRARY_NAME_MAX);
}

static int reference_matches(const AstraLibraryReference *reference,
                             const AstraLibrary *identity,
                             const char *soname)
{
    return reference != NULL &&
           identity_matches(reference->name, identity, soname) &&
           identity->major == reference->major &&
           identity->minor == reference->minor &&
           identity->patch == reference->patch &&
           identity->abi_major == reference->abi_major &&
           identity->abi_minor == reference->abi_minor &&
           identity->build_id == reference->build_id;
}

static uint32_t source_status(uint32_t status)
{
    return status == ASTRA_VFS_ERR_NOT_FOUND ? ASTRA_SYSCALL_INVALID_ARGUMENT :
           status == ASTRA_VFS_ERR_NO_SPACE ? ASTRA_SYSCALL_OUT_OF_MEMORY :
                                              ASTRA_SYSCALL_IO_ERROR;
}

static uint32_t dependency_open(void *opaque, const char *identity,
                                AstraDynamicImage *image, void **out_token)
{
    LoaderDependency *dependency;
    AstraLibraryReference reference = {0};
    AstraSyscallResult result;
    const uint8_t *header = NULL;
    const char *soname = NULL;
    char *path = NULL;
    uint32_t base = 0u;
    uint32_t span = 0u;
    uint32_t status;
    const char *failure_stage = "library provider resolve";

    const AstraBundleManifest *manifest = opaque;
    const AstraBundleLibrary *private = private_library(manifest, identity);
    *out_token = NULL;
    dependency = astra_runtime_callocate(1u, sizeof(*dependency));
    if (dependency == NULL)
        return ASTRA_SYSCALL_OUT_OF_MEMORY;
    dependency->source = (AstraVfsReadSource)ASTRA_VFS_READ_SOURCE_INIT;
    path = astra_runtime_allocate(ASTRA_VFS_PATH_MAX);
    if (path == NULL) status = ASTRA_SYSCALL_OUT_OF_MEMORY;
    else if (private != NULL) status = private_payload_source(
        private, identity, path, &dependency->source, &reference);
    else status = astra_vfs_library_resolve(
        astra_process_vfs_assigns(), "LIBS", identity,
        astra_process_vfs_assign_client, NULL,
        path, ASTRA_VFS_PATH_MAX, &reference);
    if (status == ASTRA_VFS_OK &&
        !required_version_satisfied(manifest, identity, &reference))
        status = ASTRA_VFS_ERR_INVALID;
    if (path != NULL && status != ASTRA_VFS_OK)
        status = source_status(status);
    if (status == ASTRA_SYSCALL_OK && private == NULL) {
        failure_stage = "library attach";
        status = astra_rt_library_attach(
            &reference, &base, &span, &dependency->load_handle);
    }
    if ((private != NULL && status == ASTRA_SYSCALL_OK) ||
        status == ASTRA_SYSCALL_WOULD_BLOCK ||
        status == ASTRA_SYSCALL_BAD_SYSCALL) {
        failure_stage = "library source open";
        status = dependency->source.file != ASTRA_VFS_FILE_INVALID ?
            ASTRA_VFS_OK :
            astra_process_vfs_source_open(path, &dependency->source);
        if (status != ASTRA_VFS_OK)
            status = source_status(status);
    }
    astra_runtime_deallocate(path);
    if (status == ASTRA_SYSCALL_OK &&
        dependency->source.file != ASTRA_VFS_FILE_INVALID) {
        failure_stage = "library source header";
        status = astra_stream_read_exact(
            astra_vfs_read_source_read_at, &dependency->source,
            dependency->source.length, 0u, ASTRA_EXECUTABLE_HEADER_SIZE,
            &header, NULL);
        if (status == ASTRA_SYSCALL_OK) {
            failure_stage = "library load begin";
            astra_syscall5(ASTRA_SYSCALL_LIBRARY_LOAD_BEGIN,
                           (uint32_t)(uintptr_t)header,
                           dependency->source.length, 0u, 0u, 0u, &result);
            status = result.status;
        }
        if (status == ASTRA_SYSCALL_OK) {
            failure_stage = "library load feed";
            dependency->load_handle = result.value0;
            status = astra_stream_feed(
                dependency->load_handle, dependency->source.length,
                astra_vfs_read_source_read_at, &dependency->source,
                ASTRA_SYSCALL_LIBRARY_LOAD_WRITE,
                result.value1, result.value2, NULL);
        }
        if (status == ASTRA_SYSCALL_OK) {
            failure_stage = "library load map";
            astra_syscall5(ASTRA_SYSCALL_LIBRARY_LOAD_MAP,
                           dependency->load_handle, 0u, 0u, 0u, 0u,
                           &result);
            status = result.status;
            base = result.value0;
            span = result.value1;
        }
    }
    (void)astra_vfs_read_source_close(&dependency->source);
    /* Which step refused, so the log names the cause: a syscall status, or
       LOADER_REFUSED_* above the syscall range for the checks below. */
    if (status == ASTRA_SYSCALL_OK && (base == 0u || span == 0u))
        status = LOADER_REFUSED_MAPPING;
    if (status == ASTRA_SYSCALL_OK &&
        astra_dynamic_open((uintptr_t)base, span, 0u, base, image) !=
            ASTRA_DYNAMIC_OK)
        status = LOADER_REFUSED_DYNAMIC;
    if (status == ASTRA_SYSCALL_OK &&
        astra_dynamic_soname(image, &soname) != ASTRA_DYNAMIC_OK)
        status = LOADER_REFUSED_SONAME;
    if (status == ASTRA_SYSCALL_OK &&
        span <= ASTRA_LIBRARY_FILE_OFFSET + ASTRA_LIBRARY_SIZE)
        status = LOADER_REFUSED_SPAN;
    if (status == ASTRA_SYSCALL_OK) {
        const AstraLibrary *library = (const AstraLibrary *)(uintptr_t)
            (base + ASTRA_LIBRARY_FILE_OFFSET);

        if (!reference_matches(&reference, library, soname))
            status = LOADER_REFUSED_IDENTITY;
    }
    if (status != ASTRA_SYSCALL_OK) {
        uint32_t failure = status == ASTRA_SYSCALL_OUT_OF_MEMORY ? status :
                                                                  ASTRA_SYSCALL_INVALID_ARGUMENT;

        (void)astra_log_failure(failure_stage, status);
        (void)astra_log_failure(identity, status);
        if (dependency->load_handle != 0u)
            (void)astra_close(dependency->load_handle);
        (void)astra_vfs_read_source_close(&dependency->source);
        astra_runtime_deallocate(dependency);
        return failure;
    }
    *out_token = dependency;
    return ASTRA_SYSCALL_OK;
}

static uint32_t dependency_finish(void *opaque, void *token, int commit)
{
    LoaderDependency *dependency = token;
    AstraSyscallResult result;
    uint32_t status;

    (void)opaque;
    if (dependency == NULL || dependency->load_handle == 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    if (commit) {
        astra_syscall5(ASTRA_SYSCALL_LIBRARY_LOAD_COMMIT,
                       dependency->load_handle, 0u, 0u, 0u, 0u, &result);
        status = result.status;
        if (status != ASTRA_SYSCALL_OK)
            (void)astra_close(dependency->load_handle);
    } else {
        status = astra_close(dependency->load_handle);
    }
    dependency->load_handle = 0u;
    astra_runtime_deallocate(dependency);
    return status;
}

static uint32_t call_preinitializers(const AstraDynamicImage *program)
{
    for (uint32_t index = 0u;
         index < astra_dynamic_preinitializer_count(program); ++index) {
        uint32_t address;

        if (astra_dynamic_preinitializer(program, index, &address) !=
            ASTRA_DYNAMIC_OK)
            return ASTRA_STATUS_INVALID;
        ((void (*)(void))(uintptr_t)address)();
    }
    return ASTRA_STATUS_OK;
}

static uint32_t call_initializers(const AstraDynamicProcess *process)
{
    for (uint32_t order = 0u;
         order < astra_dynamic_process_lifecycle_count(process); ++order) {
        const AstraDynamicImage *image = astra_dynamic_process_image(
            process, astra_dynamic_process_lifecycle_image(process, order));

        for (uint32_t index = 0u;
             index < astra_dynamic_initializer_count(image); ++index) {
            uint32_t address;

            if (astra_dynamic_initializer(image, index, &address) !=
                ASTRA_DYNAMIC_OK)
                return ASTRA_STATUS_INVALID;
            ((void (*)(void))(uintptr_t)address)();
        }
    }
    return ASTRA_STATUS_OK;
}

static void call_finalizers(const AstraDynamicProcess *process)
{
    uint32_t count = astra_dynamic_process_lifecycle_count(process);

    while (count != 0u) {
        const AstraDynamicImage *image = astra_dynamic_process_image(
            process,
            astra_dynamic_process_lifecycle_image(process, --count));

        for (uint32_t index = 0u;
             index < astra_dynamic_finalizer_count(image); ++index) {
            uint32_t address;

            if (astra_dynamic_finalizer(image, index, &address) !=
                ASTRA_DYNAMIC_OK)
                return;
            ((void (*)(void))(uintptr_t)address)();
        }
    }
}

extern int astra_loader_enter_program(uint32_t entry,
                                      const AstraStartupInfo *startup);

static void loader_trace(const char *stage)
{
    uint32_t length = 0u;

    while (stage[length] != '\0')
        ++length;
    (void)astra_log_debug(stage, length);
}

int astra_loader_main(const AstraStartupInfo *startup)
{
    AstraBundleManifest bundle = ASTRA_BUNDLE_MANIFEST_INIT;
    AstraDynamicImage program;
    AstraDynamicProcess *process = NULL;
    AstraDynamicTlsLayout tls;
    void *tls_template = NULL;
    uint32_t tls_storage_size = 0u;
    uint32_t status;
    uint32_t relocation_image = UINT32_MAX;
    const char *failure_operation = "dynamic dependencies";
    enum LoaderFailure failure = LOADER_FAILURE_DEPENDENCIES;
    int exit_status;

    loader_trace("dynamic loader entered");
    if (!astra_startup_validate(startup) ||
        (startup->flags & ASTRA_STARTUP_FLAG_INTERPRETED) == 0u ||
        startup->program_entry < ASTRA_PROGRAM_HEADER_ADDRESS ||
        astra_dynamic_open(0u, startup->interpreter_base,
                           ASTRA_PROGRAM_HEADER_ADDRESS, 0u, &program) !=
            ASTRA_DYNAMIC_OK)
        return loader_failure("dynamic startup", ASTRA_STATUS_INVALID,
                              LOADER_FAILURE_STARTUP);
    loader_trace("dynamic loader startup valid");
    status = astra_process_vfs_init(startup);
    if (status != ASTRA_VFS_OK)
        return loader_failure("dynamic VFS", status, LOADER_FAILURE_VFS);
    loader_trace("dynamic loader VFS ready");
    if (astra_assign_lookup(astra_process_vfs_assigns(), "APP") != NULL) {
        status = private_manifest_open(&bundle);
        if (status != ASTRA_VFS_OK) {
            failure_operation = "application bundle manifest";
            goto failed;
        }
    }
    status = astra_dynamic_process_prepare(
        &program, dependency_open, dependency_finish, &bundle, &process);
    astra_bundle_manifest_destroy(&bundle);
    if (status != ASTRA_SYSCALL_OK)
        goto failed;
    loader_trace("dynamic loader dependencies ready");
    failure_operation = "dynamic relocation";
    failure = LOADER_FAILURE_RELOCATION;
    {
        AstraDynamicStatus relocation_status =
            astra_dynamic_process_relocate(
                process, &tls, &relocation_image);

        if (relocation_status != ASTRA_DYNAMIC_OK) {
            const char *identity = astra_dynamic_process_identity(
                process, relocation_image);

            (void)astra_log_failure(
                identity != NULL ? identity : "program relocation",
                (uint32_t)relocation_status);
            status = ASTRA_SYSCALL_INVALID_ARGUMENT;
            goto failed;
        }
    }
    loader_trace("dynamic loader relocated");
    if (tls.memory_size != 0u) {
        uint32_t allocation_alignment = tls.alignment;

        failure_operation = "dynamic TLS allocation";
        failure = LOADER_FAILURE_TLS_ALLOCATION;
        if (tls.memory_size > UINT32_MAX -
                                  (ASTRA_MEMORY_PAGE_SIZE - 1u)) {
            status = ASTRA_SYSCALL_OUT_OF_MEMORY;
            goto failed;
        }
        tls_storage_size =
            (tls.memory_size + ASTRA_MEMORY_PAGE_SIZE - 1u) &
            ~(ASTRA_MEMORY_PAGE_SIZE - 1u);
        if (allocation_alignment < ASTRA_MEMORY_PAGE_SIZE)
            allocation_alignment = ASTRA_MEMORY_PAGE_SIZE;
        tls_template = astra_runtime_allocate_aligned(
            allocation_alignment, tls_storage_size);
        if (tls_template == NULL) {
            status = ASTRA_SYSCALL_OUT_OF_MEMORY;
            goto failed;
        }
        failure_operation = "dynamic TLS initialization";
        failure = LOADER_FAILURE_TLS_INITIALIZATION;
        status = astra_dynamic_process_tls_initialize(
            process, &tls, tls_template, tls.memory_size);
        if (status != ASTRA_SYSCALL_OK)
            goto failed;
    }
    failure_operation = "dynamic library commit";
    failure = LOADER_FAILURE_LIBRARY_COMMIT;
    status = astra_dynamic_process_commit(process);
    if (status != ASTRA_SYSCALL_OK)
        goto failed;
    loader_trace("dynamic loader libraries committed");
    astra_process_vfs_close();
    failure_operation = "dynamic process commit";
    failure = LOADER_FAILURE_PROCESS_COMMIT;
    status = astra_rt_process_dynamic_commit(
        tls_template, tls.memory_size, tls.alignment, tls_storage_size);
    if (status != ASTRA_SYSCALL_OK)
        goto failed;
    loader_trace("dynamic loader process committed");
    /*
     * PROCESS_DYNAMIC_COMMIT takes this allocation as the immutable source
     * for every future thread's initial-exec TLS.  The kernel protects the
     * containing pages read-only and the process address space releases them
     * at exit; returning the block to malloc would corrupt the allocator and
     * discard the retained template.
     */
    tls_template = NULL;
    failure_operation = "dynamic initializers";
    failure = LOADER_FAILURE_INITIALIZERS;
    if (call_preinitializers(&program) != ASTRA_STATUS_OK ||
        call_initializers(process) != ASTRA_STATUS_OK) {
        status = ASTRA_SYSCALL_INVALID_ARGUMENT;
        goto failed;
    }
    loader_trace("dynamic loader entering program");
    exit_status = astra_loader_enter_program(startup->program_entry, startup);
    call_finalizers(process);
    astra_dynamic_process_discard(process);
    return exit_status < 0 ?
        loader_failure("dynamic program status", (uint32_t)exit_status,
                       LOADER_FAILURE_PROGRAM_STATUS) : exit_status;

failed:
    astra_bundle_manifest_destroy(&bundle);
    astra_process_vfs_close();
    astra_runtime_deallocate(tls_template);
    astra_dynamic_process_discard(process);
    return loader_failure(failure_operation, status, failure);
}
