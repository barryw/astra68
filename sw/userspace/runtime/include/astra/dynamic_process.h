#ifndef ASTRA_USERSPACE_DYNAMIC_PROCESS_H
#define ASTRA_USERSPACE_DYNAMIC_PROCESS_H

/** @file dynamic_process.h
 * @brief Dependency-graph policy for one eagerly linked Astra process.
 *
 * The ELF parser and relocator live in dynamic_loader.h.  This layer owns the
 * one piece of process policy above them: construct an exact, acyclic
 * DT_NEEDED closure without a fixed dependency limit, retain its stable symbol
 * search order, and derive dependency-first lifecycle order.
 */

#include <stdint.h>

#include <astra/dynamic_loader.h>

/** Opaque dependency graph built for one process launch. */
typedef struct AstraDynamicProcess AstraDynamicProcess;

/**
 * Open and map one exact DT_NEEDED identity.
 *
 * The returned image must already have passed astra_dynamic_open().  `token`
 * is retained until commit or rollback and is never interpreted by the graph.
 */
typedef uint32_t (*AstraDynamicProcessOpen)(
    void *context, const char *identity, AstraDynamicImage *image,
    void **token);

/** Commit or roll back one image previously returned by the open callback. */
typedef uint32_t (*AstraDynamicProcessFinish)(
    void *context, void *token, int commit);

/**
 * Build and relocate the complete closure rooted at `program`.
 *
 * Image zero is always the main executable. Dependencies follow in stable
 * breadth-first DT_NEEDED order, which is also the global symbol search order.
 * Every dependency edge is retained so cycles can be rejected and lifecycle
 * order can be derived without recursion.
 */
uint32_t astra_dynamic_process_prepare(
    const AstraDynamicImage *program, AstraDynamicProcessOpen open_image,
    AstraDynamicProcessFinish finish_image, void *context,
    AstraDynamicProcess **process);

/**
 * Assign combined initial-exec TLS and relocate the complete closure.
 *
 * @param process Prepared dependency closure.
 * @param layout Receives the combined TLS layout.
 * @param failure_image Receives the failing image index, or `UINT32_MAX` when
 * relocation succeeds or no image was reached.
 * @return The precise parser, symbol, TLS, or relocation status.
 */
AstraDynamicStatus astra_dynamic_process_relocate(
    AstraDynamicProcess *process, AstraDynamicTlsLayout *layout,
    uint32_t *failure_image);

/** Materialize the TLS template assigned by the preceding relocate call. */
uint32_t astra_dynamic_process_tls_initialize(
    const AstraDynamicProcess *process, const AstraDynamicTlsLayout *layout,
    void *destination, uint32_t capacity);

/** Commit every dependency mapping after successful eager relocation. */
uint32_t astra_dynamic_process_commit(AstraDynamicProcess *process);

/** Roll back uncommitted mappings and release graph storage. */
void astra_dynamic_process_discard(AstraDynamicProcess *process);

/** Number of images, including the main executable. */
uint32_t astra_dynamic_process_image_count(const AstraDynamicProcess *process);

/** Image in stable global symbol-search order. */
const AstraDynamicImage *astra_dynamic_process_image(
    const AstraDynamicProcess *process, uint32_t index);

/** Exact SONAME for a dependency image, or NULL for the main executable. */
const char *astra_dynamic_process_identity(
    const AstraDynamicProcess *process, uint32_t index);

/** Number of images in dependency-first lifecycle order. */
uint32_t astra_dynamic_process_lifecycle_count(
    const AstraDynamicProcess *process);

/** Image index in dependency-first lifecycle order. */
uint32_t astra_dynamic_process_lifecycle_image(
    const AstraDynamicProcess *process, uint32_t index);

#endif
