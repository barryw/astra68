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
    }
    return result.status;
}

uint32_t
astra_process_exec(const void *image, uint32_t length,
                   const AstraExecRequest *request)
{
    AstraSyscallResult result;

    if (image == NULL || length == 0u || request == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    astra_syscall5(ASTRA_SYSCALL_PROCESS_EXEC,
                   (uint32_t)(uintptr_t)image, length,
                   (uint32_t)(uintptr_t)request, 0u, 0u, &result);
    return result.status;
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
