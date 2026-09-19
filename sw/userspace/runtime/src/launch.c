/*
 * Starting a program, from the launcher's side.
 *
 * Two wrappers and no policy. Every rule about what a launch may hand over --
 * that a grant names a handle the caller holds, that its rights are a subset of
 * the ones it holds it by, that a malformed image leaves nothing behind -- is
 * enforced in the kernel, where it cannot be got around by calling the syscall
 * directly. Repeating any of it here would be a second answer to a question
 * that has to have one.
 *
 * What is left is marshalling, and one promise the syscall does not make on its
 * own: an output that is never left carrying what was in it before the call.
 */

#include <astra/runtime.h>
#include <astra/syscall.h>

#include <string.h>

#include "stream_source.h"

extern void astra_runtime_forget_current_thread_handle(void);

static uint64_t load_elapsed(uint64_t start)
{
    uint64_t finish = astra_clock_monotonic();

    return finish >= start ? finish - start : 0u;
}

static int load_profile_prepare(AstraProcessLoadProfile *profile)
{
    if (profile == NULL)
        return 1;
    if (profile->size != ASTRA_PROCESS_LOAD_PROFILE_SIZE ||
        profile->reserved != 0u)
        return 0;
    *profile = (AstraProcessLoadProfile){
        .size = ASTRA_PROCESS_LOAD_PROFILE_SIZE,
    };
    return 1;
}

static uint64_t load_profile_start(const AstraProcessLoadProfile *profile)
{
    return profile != NULL ? astra_clock_monotonic() : 0u;
}

static void load_profile_finish(AstraProcessLoadProfile *profile,
                                uint64_t start)
{
    if (profile != NULL)
        profile->total_ns = load_elapsed(start);
}

static void load_profile_operation(AstraProcessLoadProfile *profile,
                                   uint64_t start, uint64_t *duration)
{
    if (profile != NULL)
        *duration += load_elapsed(start);
}

uint32_t
astra_launch(const void *image, uint32_t length,
             const AstraLaunchGrant *grants, uint32_t count,
             const AstraLaunchArguments *arguments, uint32_t *process_handle,
             uint32_t *process_id)
{
    AstraSyscallResult result;

    /*
     * Cleared before anything else, including the refusal below. A launcher
     * that reads a handle out of a launch that did not happen closes a handle
     * belonging to something else, and that is a fault a long way from here.
     */
    if (process_handle != NULL) {
        *process_handle = 0u;
    }
    if (process_id != NULL) {
        *process_id = 0u;
    }
    if (process_handle == NULL || process_id == NULL) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    astra_syscall5(ASTRA_SYSCALL_PROCESS_CREATE, (uint32_t)(uintptr_t)image,
                   length, (uint32_t)(uintptr_t)grants, count,
                   (uint32_t)(uintptr_t)arguments, &result);
    if (result.status == ASTRA_SYSCALL_OK) {
        *process_handle = result.value0;
        *process_id = result.value1;
    }
    return result.status;
}

uint32_t astra_launch_stream(
    uint32_t length, AstraReadAt read_at, AstraSourceRelease release,
    void *context,
    const AstraLaunchGrant *grants, uint32_t count,
    const AstraLaunchArguments *arguments, AstraProcessLoadProfile *profile,
    uint32_t *process_handle, uint32_t *process_id)
{
    const uint8_t *header;
    AstraSyscallResult result;
    uint32_t load_handle = 0u;
    uint32_t status;
    uint32_t released = 0u;
    uint64_t total_start;

    if (process_handle != NULL)
        *process_handle = 0u;
    if (process_id != NULL)
        *process_id = 0u;
    if (read_at == NULL || release == NULL || process_handle == NULL ||
        process_id == NULL || !load_profile_prepare(profile))
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    total_start = load_profile_start(profile);
    if (length < ASTRA_EXECUTABLE_HEADER_SIZE) {
        uint64_t start = load_profile_start(profile);

        (void)release(context);
        load_profile_operation(profile, start,
                               profile != NULL ? &profile->source_release_ns :
                                                 NULL);
        load_profile_finish(profile, total_start);
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    status = astra_stream_read_exact(read_at, context, length, 0u,
                                     ASTRA_EXECUTABLE_HEADER_SIZE, &header,
                                     profile);
    if (status != ASTRA_SYSCALL_OK)
        goto failed;
    {
        uint64_t start = load_profile_start(profile);

        astra_syscall5(ASTRA_SYSCALL_PROCESS_LOAD_BEGIN,
                       (uint32_t)(uintptr_t)header, length, 0u, 0u, 0u,
                       &result);
        load_profile_operation(profile, start,
                               profile != NULL ? &profile->kernel_begin_ns :
                                                 NULL);
    }
    if (result.status != ASTRA_SYSCALL_OK) {
        status = result.status;
        goto failed;
    }
    load_handle = result.value0;
    status = astra_stream_feed(load_handle, length, read_at, context,
                               ASTRA_SYSCALL_PROCESS_LOAD_WRITE,
                               result.value1, result.value2, profile);
    if (status != ASTRA_SYSCALL_OK)
        goto failed;
    {
        uint64_t start = load_profile_start(profile);

        astra_syscall5(ASTRA_SYSCALL_PROCESS_LOAD_CREATE, load_handle,
                       (uint32_t)(uintptr_t)grants, count,
                       (uint32_t)(uintptr_t)arguments, 0u, &result);
        load_profile_operation(profile, start,
                               profile != NULL ? &profile->kernel_create_ns :
                                                 NULL);
    }
    if (result.status != ASTRA_SYSCALL_OK) {
        status = result.status;
        goto failed;
    }
    status = astra_stream_feed(load_handle, length, read_at, context,
                               ASTRA_SYSCALL_PROCESS_LOAD_WRITE,
                               result.value0, result.value1, profile);
    if (status != ASTRA_SYSCALL_OK)
        goto failed;
    released = 1u;
    {
        uint64_t start = load_profile_start(profile);
        uint32_t release_status = release(context);

        load_profile_operation(profile, start,
                               profile != NULL ? &profile->source_release_ns :
                                                 NULL);
        if (release_status != 0u) {
            status = ASTRA_SYSCALL_IO_ERROR;
            goto failed;
        }
    }
    {
        uint64_t start = load_profile_start(profile);

        astra_syscall5(ASTRA_SYSCALL_PROCESS_LOAD_COMMIT, load_handle, 0u,
                       0u, 0u, 0u, &result);
        load_profile_operation(profile, start,
                               profile != NULL ? &profile->kernel_commit_ns :
                                                 NULL);
    }
    if (result.status != ASTRA_SYSCALL_OK) {
        status = result.status;
        goto failed;
    }
    *process_handle = result.value0;
    *process_id = result.value1;
    load_profile_finish(profile, total_start);
    return ASTRA_SYSCALL_OK;

failed:
    if (released == 0u) {
        uint64_t start = load_profile_start(profile);

        (void)release(context);
        load_profile_operation(profile, start,
                               profile != NULL ? &profile->source_release_ns :
                                                 NULL);
    }
    if (load_handle != 0u)
        (void)astra_close(load_handle);
    load_profile_finish(profile, total_start);
    return status;
}

static uint32_t release_dynamic_sources(
    const AstraReadSource *program,
    const AstraReadSource *interpreter,
    uint32_t *program_released, uint32_t *interpreter_released)
{
    uint32_t failed = 0u;

    if (*program_released == 0u) {
        *program_released = 1u;
        if (program->release(program->context) != 0u)
            failed = 1u;
    }
    if (*interpreter_released == 0u) {
        *interpreter_released = 1u;
        if (interpreter->release(interpreter->context) != 0u)
            failed = 1u;
    }
    return failed != 0u ? ASTRA_SYSCALL_IO_ERROR : ASTRA_SYSCALL_OK;
}

uint32_t astra_launch_dynamic_stream(
    const AstraReadSource *program,
    const AstraReadSource *interpreter,
    const AstraLaunchGrant *grants, uint32_t count,
    const AstraLaunchArguments *arguments, AstraProcessLoadProfile *profile,
    uint32_t *process_handle, uint32_t *process_id)
{
    const uint8_t *header;
    AstraSyscallResult result;
    uint32_t load_handle = 0u;
    uint32_t program_released = 0u;
    uint32_t interpreter_released = 0u;
    uint32_t status;
    uint64_t total_start;

    if (process_handle != NULL)
        *process_handle = 0u;
    if (process_id != NULL)
        *process_id = 0u;
    if (program == NULL || interpreter == NULL ||
        program->read_at == NULL || program->release == NULL ||
        interpreter->read_at == NULL || interpreter->release == NULL ||
        process_handle == NULL || process_id == NULL ||
        !load_profile_prepare(profile))
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    total_start = load_profile_start(profile);
    if (program->length < ASTRA_EXECUTABLE_HEADER_SIZE ||
        interpreter->length < ASTRA_EXECUTABLE_HEADER_SIZE) {
        status = ASTRA_SYSCALL_INVALID_ARGUMENT;
        goto failed;
    }

    status = astra_stream_read_exact(
        program->read_at, program->context, program->length, 0u,
        ASTRA_EXECUTABLE_HEADER_SIZE, &header, profile);
    if (status != ASTRA_SYSCALL_OK)
        goto failed;
    {
        uint64_t start = load_profile_start(profile);

        astra_syscall5(ASTRA_SYSCALL_PROCESS_LOAD_BEGIN,
                       (uint32_t)(uintptr_t)header, program->length,
                       0u, 0u, 0u, &result);
        load_profile_operation(profile, start,
                               profile != NULL ? &profile->kernel_begin_ns :
                                                 NULL);
    }
    if (result.status != ASTRA_SYSCALL_OK) {
        status = result.status;
        goto failed;
    }
    load_handle = result.value0;
    status = astra_stream_feed(
        load_handle, program->length, program->read_at, program->context,
        ASTRA_SYSCALL_PROCESS_LOAD_WRITE, result.value1, result.value2,
        profile);
    if (status != ASTRA_SYSCALL_OK)
        goto failed;

    status = astra_stream_read_exact(
        interpreter->read_at, interpreter->context, interpreter->length, 0u,
        ASTRA_EXECUTABLE_HEADER_SIZE, &header, profile);
    if (status != ASTRA_SYSCALL_OK)
        goto failed;
    {
        uint64_t start = load_profile_start(profile);

        astra_syscall5(ASTRA_SYSCALL_PROCESS_LOAD_INTERPRETER, load_handle,
                       (uint32_t)(uintptr_t)header, interpreter->length,
                       0u, 0u, &result);
        load_profile_operation(
            profile, start,
            profile != NULL ? &profile->kernel_interpreter_ns : NULL);
    }
    if (result.status != ASTRA_SYSCALL_OK) {
        status = result.status;
        goto failed;
    }
    status = astra_stream_feed(
        load_handle, interpreter->length, interpreter->read_at,
        interpreter->context, ASTRA_SYSCALL_PROCESS_LOAD_WRITE,
        result.value0, result.value1, profile);
    if (status != ASTRA_SYSCALL_OK)
        goto failed;

    {
        uint64_t start = load_profile_start(profile);

        astra_syscall5(ASTRA_SYSCALL_PROCESS_LOAD_CREATE, load_handle,
                       (uint32_t)(uintptr_t)grants, count,
                       (uint32_t)(uintptr_t)arguments, 0u, &result);
        load_profile_operation(profile, start,
                               profile != NULL ? &profile->kernel_create_ns :
                                                 NULL);
    }
    if (result.status != ASTRA_SYSCALL_OK) {
        status = result.status;
        goto failed;
    }
    while (result.value1 != 0u) {
        const AstraReadSource *source;
        const uint8_t *bytes;
        uint32_t moved = 0u;

        if (result.value2 == ASTRA_PROCESS_LOAD_SOURCE_PROGRAM)
            source = program;
        else if (result.value2 == ASTRA_PROCESS_LOAD_SOURCE_INTERPRETER)
            source = interpreter;
        else {
            status = ASTRA_SYSCALL_INVALID_ARGUMENT;
            goto failed;
        }
        status = astra_stream_read_exact(
            source->read_at, source->context, source->length,
            result.value0, result.value1, &bytes, profile);
        if (status != ASTRA_SYSCALL_OK)
            goto failed;
        moved = result.value1;
        {
            uint64_t start = load_profile_start(profile);

            if (profile != NULL) {
                ++profile->kernel_writes;
                profile->kernel_write_bytes += moved;
            }
            astra_syscall5(ASTRA_SYSCALL_PROCESS_LOAD_WRITE, load_handle,
                           result.value0, (uint32_t)(uintptr_t)bytes, moved,
                           0u, &result);
            load_profile_operation(
                profile, start,
                profile != NULL ? &profile->kernel_write_ns : NULL);
        }
        if (result.status != ASTRA_SYSCALL_OK) {
            status = result.status;
            goto failed;
        }
    }

    {
        uint64_t start = load_profile_start(profile);

        status = release_dynamic_sources(
            program, interpreter, &program_released, &interpreter_released);
        load_profile_operation(profile, start,
                               profile != NULL ? &profile->source_release_ns :
                                                 NULL);
    }
    if (status != ASTRA_SYSCALL_OK)
        goto failed;
    {
        uint64_t start = load_profile_start(profile);

        astra_syscall5(ASTRA_SYSCALL_PROCESS_LOAD_COMMIT, load_handle,
                       0u, 0u, 0u, 0u, &result);
        load_profile_operation(profile, start,
                               profile != NULL ? &profile->kernel_commit_ns :
                                                 NULL);
    }
    if (result.status != ASTRA_SYSCALL_OK) {
        status = result.status;
        goto failed;
    }
    *process_handle = result.value0;
    *process_id = result.value1;
    load_profile_finish(profile, total_start);
    return ASTRA_SYSCALL_OK;

failed:
    {
        uint64_t start = load_profile_start(profile);

        (void)release_dynamic_sources(program, interpreter,
                                      &program_released,
                                      &interpreter_released);
        load_profile_operation(profile, start,
                               profile != NULL ? &profile->source_release_ns :
                                                 NULL);
    }
    if (load_handle != 0u)
        (void)astra_close(load_handle);
    load_profile_finish(profile, total_start);
    return status;
}

uint32_t astra_launch_executable_stream(
    const AstraReadSource *program,
    AstraInterpreterOpen open_interpreter, void *interpreter_context,
    const AstraLaunchGrant *grants, uint32_t count,
    const AstraLaunchArguments *arguments,
    AstraExecutableLoadProfile *profile,
    uint32_t *process_handle, uint32_t *process_id)
{
    AstraReadSource interpreter = {0};
    char identity[ASTRA_LIBRARY_NAME_MAX + 6u];
    uint32_t identity_length = 0u;
    uint32_t status;
    uint64_t started;

    if (process_handle != NULL)
        *process_handle = 0u;
    if (process_id != NULL)
        *process_id = 0u;
    if (program == NULL || program->read_at == NULL ||
        program->release == NULL || process_handle == NULL ||
        process_id == NULL ||
        (profile != NULL &&
         (profile->size != ASTRA_EXECUTABLE_LOAD_PROFILE_SIZE ||
          profile->reserved != 0u)))
        return ASTRA_SYSCALL_INVALID_ARGUMENT;

    if (profile != NULL) {
        *profile = (AstraExecutableLoadProfile){
            .size = ASTRA_EXECUTABLE_LOAD_PROFILE_SIZE,
            .transaction = {
                .size = ASTRA_PROCESS_LOAD_PROFILE_SIZE,
            },
        };
    }

    started = profile != NULL ? astra_clock_monotonic() : 0u;
    status = astra_executable_interpreter(
        program, identity, sizeof(identity), &identity_length);
    if (profile != NULL)
        profile->executable_probe_ns = load_elapsed(started);
    if (status != ASTRA_SYSCALL_OK) {
        if (program->release(program->context) != ASTRA_SYSCALL_OK)
            return ASTRA_SYSCALL_IO_ERROR;
        return status;
    }
    if (identity_length == 0u)
        return astra_launch_stream(
            program->length, program->read_at, program->release,
            program->context, grants, count, arguments,
            profile != NULL ? &profile->transaction : NULL,
            process_handle, process_id);
    if (open_interpreter == NULL) {
        if (program->release(program->context) != ASTRA_SYSCALL_OK)
            return ASTRA_SYSCALL_IO_ERROR;
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    started = profile != NULL ? astra_clock_monotonic() : 0u;
    status = open_interpreter(interpreter_context, identity, &interpreter);
    if (profile != NULL)
        profile->interpreter_open_ns = load_elapsed(started);
    if (status != ASTRA_SYSCALL_OK) {
        if (program->release(program->context) != ASTRA_SYSCALL_OK)
            return ASTRA_SYSCALL_IO_ERROR;
        return status;
    }
    if (interpreter.read_at == NULL || interpreter.release == NULL) {
        uint32_t release_failed = 0u;

        if (program->release(program->context) != ASTRA_SYSCALL_OK)
            release_failed = 1u;
        if (interpreter.release != NULL &&
            interpreter.release(interpreter.context) != ASTRA_SYSCALL_OK)
            release_failed = 1u;
        return release_failed != 0u ? ASTRA_SYSCALL_IO_ERROR :
                                      ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    return astra_launch_dynamic_stream(
        program, &interpreter, grants, count, arguments,
        profile != NULL ? &profile->transaction : NULL,
        process_handle, process_id);
}

static uint32_t materialize_source(const AstraReadSource *source,
                                   uint8_t **image)
{
    uint8_t *storage;
    uint32_t offset = 0u;

    if (source == NULL || image == NULL || source->length == 0u ||
        source->read_at == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    *image = NULL;
    storage = astra_runtime_allocate(source->length);
    if (storage == NULL)
        return ASTRA_SYSCALL_OUT_OF_MEMORY;
    while (offset != source->length) {
        const uint8_t *bytes;
        uint32_t length = source->length - offset;
        uint32_t status;

        if (length > 4096u)
            length = 4096u;
        status = astra_stream_read_exact(
            source->read_at, source->context, source->length, offset,
            length, &bytes, NULL);
        if (status != ASTRA_SYSCALL_OK) {
            astra_runtime_deallocate(storage);
            return status;
        }
        memcpy(storage + offset, bytes, length);
        offset += length;
    }
    *image = storage;
    return ASTRA_SYSCALL_OK;
}

static uint32_t process_exec_images(
    const void *program, uint32_t program_length,
    const void *interpreter, uint32_t interpreter_length,
    const AstraExecRequest *request)
{
    AstraSyscallResult result;

    if (program == NULL || program_length == 0u || request == NULL ||
        (interpreter == NULL) != (interpreter_length == 0u))
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    astra_syscall5(ASTRA_SYSCALL_PROCESS_EXEC,
                   (uint32_t)(uintptr_t)program, program_length,
                   (uint32_t)(uintptr_t)request,
                   (uint32_t)(uintptr_t)interpreter, interpreter_length,
                   &result);
    return result.status;
}

uint32_t astra_exec_executable_stream(
    const AstraReadSource *program,
    AstraInterpreterOpen open_interpreter, void *interpreter_context,
    AstraExecPrepare prepare, void *prepare_context,
    AstraExecRequest *request)
{
    AstraReadSource interpreter = {0};
    uint8_t *program_image = NULL;
    uint8_t *interpreter_image = NULL;
    char identity[ASTRA_LIBRARY_NAME_MAX + 6u];
    uint32_t identity_length = 0u;
    uint32_t program_released = 0u;
    uint32_t interpreter_released = 0u;
    uint32_t interpreter_opened = 0u;
    uint32_t status;

    if (program == NULL || program->read_at == NULL ||
        program->release == NULL || request == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    (void)astra_log_debug("exec stream entered", 19u);
    status = astra_executable_interpreter(
        program, identity, sizeof(identity), &identity_length);
    if (status != ASTRA_SYSCALL_OK)
        goto failed;
    (void)astra_log_debug("exec interpreter identified", 27u);
    if (identity_length != 0u) {
        if (open_interpreter == NULL) {
            status = ASTRA_SYSCALL_INVALID_ARGUMENT;
            goto failed;
        }
        status = open_interpreter(interpreter_context, identity,
                                  &interpreter);
        if (status != ASTRA_SYSCALL_OK)
            goto failed;
        (void)astra_log_debug("exec interpreter opened", 23u);
        interpreter_opened = 1u;
        if (interpreter.read_at == NULL || interpreter.release == NULL) {
            status = ASTRA_SYSCALL_INVALID_ARGUMENT;
            goto failed;
        }
    }
    status = materialize_source(program, &program_image);
    if (status != ASTRA_SYSCALL_OK)
        goto failed;
    (void)astra_log_debug("exec program materialized", 25u);
    if (identity_length != 0u) {
        status = materialize_source(&interpreter, &interpreter_image);
        if (status != ASTRA_SYSCALL_OK)
            goto failed;
        (void)astra_log_debug("exec interpreter materialized", 29u);
        status = release_dynamic_sources(
            program, &interpreter, &program_released,
            &interpreter_released);
    } else {
        program_released = 1u;
        status = program->release(program->context) == 0u ?
            ASTRA_SYSCALL_OK : ASTRA_SYSCALL_IO_ERROR;
    }
    if (status != ASTRA_SYSCALL_OK)
        goto failed;
    (void)astra_log_debug("exec sources released", 21u);
    if (prepare != NULL) {
        (void)astra_log_debug("exec prepare entered", 20u);
        status = prepare(prepare_context, request);
        if (status != ASTRA_SYSCALL_OK)
            goto failed;
        (void)astra_log_debug("exec prepare complete", 21u);
    }
    (void)astra_log_debug("exec syscall entered", 20u);
    status = process_exec_images(
        program_image, program->length, interpreter_image,
        identity_length != 0u ? interpreter.length : 0u, request);

failed:
    if (program_released == 0u) {
        program_released = 1u;
        if (program->release(program->context) != 0u &&
            status == ASTRA_SYSCALL_OK)
            status = ASTRA_SYSCALL_IO_ERROR;
    }
    if (interpreter_opened != 0u && interpreter_released == 0u &&
        interpreter.release != NULL) {
        interpreter_released = 1u;
        if (interpreter.release(interpreter.context) != 0u &&
            status == ASTRA_SYSCALL_OK)
            status = ASTRA_SYSCALL_IO_ERROR;
    }
    astra_runtime_deallocate(interpreter_image);
    astra_runtime_deallocate(program_image);
    return status;
}

uint32_t
astra_process_clone(uint32_t *process_handle, uint32_t *process_id)
{
    AstraSyscallResult result;

    if (process_handle == NULL || process_id == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    *process_handle = 0u;
    *process_id = 0u;
    astra_syscall5(ASTRA_SYSCALL_PROCESS_CLONE, 0u, 0u, 0u, 0u, 0u,
                   &result);
    if (result.status == ASTRA_SYSCALL_OK) {
        *process_handle = result.value0;
        *process_id = result.value1;
        if (result.value1 == 0u)
            astra_runtime_forget_current_thread_handle();
    }
    return result.status;
}

uint32_t
astra_process_signal(uint32_t process_handle, uint32_t signal)
{
    AstraSyscallResult result;

    if (process_handle == 0u || signal == 0u || signal >= 32u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    astra_syscall5(ASTRA_SYSCALL_PROCESS_SIGNAL, process_handle, signal,
                   0u, 0u, 0u, &result);
    return result.status;
}

uint32_t
astra_process_terminate(uint32_t process_handle, uint32_t reason)
{
    AstraSyscallResult result;

    if (process_handle == 0u || reason == 0u || reason >= 32u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    astra_syscall5(ASTRA_SYSCALL_PROCESS_TERMINATE, process_handle, reason,
                   0u, 0u, 0u, &result);
    return result.status;
}

uint32_t
astra_process_suspend(uint32_t process_handle)
{
    AstraSyscallResult result;

    if (process_handle == 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    astra_syscall5(ASTRA_SYSCALL_PROCESS_SUSPEND, process_handle, 0u,
                   0u, 0u, 0u, &result);
    return result.status;
}

uint32_t
astra_process_resume(uint32_t process_handle)
{
    AstraSyscallResult result;

    if (process_handle == 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    astra_syscall5(ASTRA_SYSCALL_PROCESS_RESUME, process_handle, 0u,
                   0u, 0u, 0u, &result);
    return result.status;
}

uint32_t
astra_process_exec(const void *image, uint32_t length,
                   const AstraExecRequest *request)
{
    return process_exec_images(image, length, NULL, 0u, request);
}

uint32_t
astra_process_wait(uint32_t handle, uint64_t deadline_ns, uint32_t *exit_status)
{
    uint32_t detail = 0u;
    uint32_t status;

    if (exit_status == NULL) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    *exit_status = 0u;
    status = astra_wait_one(handle, deadline_ns, &detail);
    /*
     * A clean exit and a fault are both a child that finished, and both carry a
     * status its launcher has to be able to report. Everything else -- a poll
     * that found it still running, a handle that is not one -- established
     * nothing, and a zero there is the absence of an answer rather than one.
     */
    if (status == ASTRA_SYSCALL_OK || status == ASTRA_SYSCALL_PEER_DEAD) {
        *exit_status = detail;
    }
    return status;
}
