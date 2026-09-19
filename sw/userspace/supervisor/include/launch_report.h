#ifndef ASTRA_SUPERVISOR_LAUNCH_REPORT_H
#define ASTRA_SUPERVISOR_LAUNCH_REPORT_H

#include <stdint.h>

typedef struct SupervisorLaunchReportField {
    const char *label;
    uint64_t value;
} SupervisorLaunchReportField;

uint32_t supervisor_launch_report_format(
    char *out, uint32_t capacity, const char *prefix, const char *path,
    const SupervisorLaunchReportField *fields, uint32_t field_count);

#endif
