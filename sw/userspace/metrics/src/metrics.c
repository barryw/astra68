#include <astra/bytes.h>
#include <astra/metrics.h>

static AstraMetricGroup registry[ASTRA_METRIC_GROUP_MAX];
static uint32_t registry_count;

AstraMetricStatus
astra_metric_register(const char *name, AstraMetricSampler sample,
                      void *context)
{
    uint32_t index;

    if (name == NULL || name[0] == '\0' || sample == NULL) {
        return ASTRA_METRIC_INVALID_ARGUMENT;
    }
    for (index = 0u; index < registry_count; ++index) {
        if (strcmp(registry[index].name, name) == 0) {
            return ASTRA_METRIC_DUPLICATE;
        }
    }
    if (registry_count == ASTRA_METRIC_GROUP_MAX) {
        return ASTRA_METRIC_FULL;
    }

    registry[registry_count].name = name;
    registry[registry_count].sample = sample;
    registry[registry_count].context = context;
    ++registry_count;
    return ASTRA_METRIC_OK;
}

uint32_t
astra_metric_group_count(void)
{
    return registry_count;
}

const AstraMetricGroup *
astra_metric_group(uint32_t index)
{
    return index < registry_count ? &registry[index] : NULL;
}

const AstraMetricGroup *
astra_metric_find(const char *name)
{
    uint32_t index;

    if (name == NULL) {
        return NULL;
    }
    for (index = 0u; index < registry_count; ++index) {
        if (strcmp(registry[index].name, name) == 0) {
            return &registry[index];
        }
    }
    return NULL;
}

uint32_t
astra_metric_sample(const AstraMetricGroup *group, AstraMetricSample *out,
                    uint32_t capacity)
{
    uint32_t written;

    if (group == NULL || group->sample == NULL || out == NULL ||
        capacity == 0u) {
        return 0u;
    }
    written = group->sample(group->context, out, capacity);
    /*
     * A sampler that reports more than it was given would have overrun the
     * caller's array. Refuse the whole sample rather than trust the count.
     */
    return written <= capacity ? written : 0u;
}

void
astra_metric_reset_registry(void)
{
    registry_count = 0u;
}
