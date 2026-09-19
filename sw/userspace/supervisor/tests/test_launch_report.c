#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <astra/vfs_service.h>

#include <launch_report.h>

int main(void)
{
    SupervisorLaunchReportField fields[] = {
        {" bytes=", UINT32_MAX},
        {" open=", UINT32_MAX},
        {" probe=", UINT32_MAX},
        {" interp=", UINT32_MAX},
        {" load=", UINT32_MAX},
        {" ready=", UINT32_MAX},
        {"us status=", UINT32_MAX},
    };
    char path[ASTRA_VFS_PATH_MAX];
    char output[512];
    uint32_t length;

    memset(path, 'p', sizeof(path) - 1u);
    path[sizeof(path) - 1u] = '\0';
    length = supervisor_launch_report_format(
        NULL, 0u, "launch ", path, fields,
        (uint32_t)(sizeof(fields) / sizeof(fields[0])));
    assert(length > 224u);
    assert(supervisor_launch_report_format(
               output, sizeof(output), "launch ", path, fields,
               (uint32_t)(sizeof(fields) / sizeof(fields[0]))) == length);
    assert(strlen(output) == length);
    assert(strstr(output, path) == output + sizeof("launch ") - 1u);
    assert(strstr(output, "us status=4294967295") != NULL);

    output[0] = 'x';
    assert(supervisor_launch_report_format(
               output, length, "launch ", path, fields,
               (uint32_t)(sizeof(fields) / sizeof(fields[0]))) == 0u);
    assert(output[0] == '\0');
    assert(supervisor_launch_report_format(
               output, sizeof(output), NULL, path, fields,
               (uint32_t)(sizeof(fields) / sizeof(fields[0]))) == 0u);
    return 0;
}
