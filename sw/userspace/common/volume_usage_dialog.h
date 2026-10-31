#ifndef ASTRA_VOLUME_USAGE_DIALOG_H
#define ASTRA_VOLUME_USAGE_DIALOG_H

#include <astra/control.h>
#include <astra/scroll.h>
#include <astra/surface.h>
#include <astra/vfs_process.h>
#include <astra/volume.h>
#include <astra/window.h>

/* Shared modal view of the exact resources preventing safe removal. */
typedef struct AstraVolumeUsageDialogRow {
    char *text;
    uint32_t length, y, height;
} AstraVolumeUsageDialogRow;

typedef struct AstraVolumeUsageDialog {
    AstraWindow window;
    AstraSharedSurface surface;
    AstraControl controls[5];
    AstraUIContext ui;
    AstraScrollModel scroll;
    AstraVolumeUsageDialogRow *rows;
    uint32_t count, crc, pointer_shape;
} AstraVolumeUsageDialog;

void astra_volume_usage_dialog_close(AstraVolumeUsageDialog *dialog);
AstraResult astra_volume_usage_dialog_open(AstraVolumeUsageDialog *dialog,
    AstraHandle gui, AstraHandle inspector, AstraProcessFilesystem *filesystem,
    const AstraVolumeReference *reference);
AstraResult astra_volume_usage_dialog_event(AstraVolumeUsageDialog *dialog,
    AstraHandle gui);
void astra_volume_usage_dialog_error(AstraHandle gui, const char *message,
    AstraResult failure);

#endif
