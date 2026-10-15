#ifndef ASTRA_VOLUME_CLI_H
#define ASTRA_VOLUME_CLI_H

#include <astra/volume.h>

typedef struct VolumeCli {
    AstraHandle metadata;
    AstraHandle administrator;
    AstraVolumeWatch watch;
} VolumeCli;

int volume_cli_open(VolumeCli *cli, const char *command);
void volume_cli_close(VolumeCli *cli);
int volume_cli_number(const char *text, uint64_t *number);
int volume_cli_select(VolumeCli *cli, const char *selector, uint32_t kind,
    AstraVolumeRecord *record, AstraVolumeReference *reference,
    AstraVolumeReference *parent);
int volume_cli_wait(VolumeCli *cli, const AstraVolumeReference *reference,
    uint32_t wanted, int disappearance_completes, AstraVolumeRecord *record);
int volume_cli_apply(VolumeCli *cli, const AstraVolumeControl *control,
    AstraVolumeReference *reference);
int volume_cli_mount(VolumeCli *cli, const AstraVolumeReference *source,
    uint32_t flags, uint64_t bytes, uint64_t inodes);

#endif
