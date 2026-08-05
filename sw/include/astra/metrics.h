#ifndef ASTRA_METRICS_H
#define ASTRA_METRICS_H

#include <stddef.h>
#include <stdint.h>

/*
 * Astra observability contract.
 *
 * Every module that does measurable work keeps its own counters in whatever
 * shape suits it, and publishes one group here. A group is a name plus a
 * sampler: the reader asks for values, the module fills in a bounded array.
 * Nothing shares a struct layout, so a module never has to change its
 * accounting to stay readable, and a reader never has to know what modules
 * exist.
 *
 * That indirection is the point. The introspection filesystem, a terminal
 * command, a log drain, and a test harness are all just readers walking this
 * registry. None of them is a special case wired into the modules themselves.
 *
 * The registry is fixed-size and allocation-free. Registration happens once
 * during module initialisation and is never undone: a group's storage must
 * outlive the process, which in practice means static storage.
 */

#define ASTRA_METRIC_GROUP_MAX 32u
#define ASTRA_METRIC_SAMPLE_MAX 64u

typedef struct AstraMetricSample {
    const char *name;
    uint64_t value;
} AstraMetricSample;

/*
 * Fill up to `capacity` samples and return how many were written. A sampler
 * must not allocate, must not block, and must be safe to call at any time,
 * including while the module it reports on is running. Truncation to capacity
 * is the sampler's responsibility and is not an error.
 */
typedef uint32_t (*AstraMetricSampler)(void *context, AstraMetricSample *out,
                                       uint32_t capacity);

typedef struct AstraMetricGroup {
    const char *name;
    AstraMetricSampler sample;
    void *context;
} AstraMetricGroup;

typedef enum AstraMetricStatus {
    ASTRA_METRIC_OK = 0,
    ASTRA_METRIC_INVALID_ARGUMENT = 1,
    ASTRA_METRIC_DUPLICATE = 2,
    ASTRA_METRIC_FULL = 3
} AstraMetricStatus;

AstraMetricStatus astra_metric_register(const char *name,
                                        AstraMetricSampler sample,
                                        void *context);
uint32_t astra_metric_group_count(void);
const AstraMetricGroup *astra_metric_group(uint32_t index);
const AstraMetricGroup *astra_metric_find(const char *name);

/*
 * Sample one group into a caller-supplied array. Returns the number of samples
 * written, or 0 for an unknown group.
 */
uint32_t astra_metric_sample(const AstraMetricGroup *group,
                             AstraMetricSample *out, uint32_t capacity);

/* Test and re-initialisation support; not part of normal service startup. */
void astra_metric_reset_registry(void);

/*
 * Standard shape for an operation class: how many times it ran, how many
 * failed, how much work it moved, and what it cost. `units` is per-group and
 * named by the module: sectors for a block device, bytes for a transport,
 * entries for a directory read.
 *
 * Durations are in clock ticks from an injected clock, never from a hardcoded
 * time source. A service under measurement supplies a real clock; a service in
 * production may supply none and pay nothing. When a cheap user-readable cycle
 * counter exists, it swaps in behind this same injection point without any
 * module changing.
 */
typedef struct AstraOpMetrics {
    uint64_t calls;
    uint64_t failures;
    uint64_t units;
    uint64_t ticks;
    uint64_t maximum_ticks;
} AstraOpMetrics;

typedef uint64_t (*AstraClock)(void *context);

static inline uint64_t astra_clock_read(AstraClock clock, void *context)
{
    return clock != NULL ? clock(context) : 0u;
}

static inline void astra_op_record(AstraOpMetrics *op, int failed,
                                   uint64_t units, uint64_t elapsed)
{
    ++op->calls;
    if (failed) {
        ++op->failures;
    }
    op->units += units;
    op->ticks += elapsed;
    if (elapsed > op->maximum_ticks) {
        op->maximum_ticks = elapsed;
    }
}

/*
 * Emit one operation class as the five standard samples. `names` must hold
 * five stable strings prefixed by the operation, e.g. "read.calls". Returns
 * the number written, which is 0 when fewer than five slots remain.
 *
 * Inline so that publishing metrics costs a module only this header. Linking
 * the registry library is the collector's job, not the publisher's.
 */
static inline uint32_t astra_op_samples(const AstraOpMetrics *op,
                                        const char *const *names,
                                        AstraMetricSample *out,
                                        uint32_t capacity)
{
    if (op == NULL || names == NULL || out == NULL || capacity < 5u) {
        return 0u;
    }
    out[0].name = names[0];
    out[0].value = op->calls;
    out[1].name = names[1];
    out[1].value = op->failures;
    out[2].name = names[2];
    out[2].value = op->units;
    out[3].name = names[3];
    out[3].value = op->ticks;
    out[4].name = names[4];
    out[4].value = op->maximum_ticks;
    return 5u;
}

#endif
