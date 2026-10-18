#ifndef ASTRA_FONT_IMPORT_TEST_SUPPORT_H
#define ASTRA_FONT_IMPORT_TEST_SUPPORT_H

#include <astra/fonts.h>
#include <astra/result.h>
#include <astra/runtime.h>
#include <string.h>

/* Headless probes wait on the same public session used by the native UI. */
static AstraResult finish_font_import(AstraFontImport *job, char id[ASTRA_FONT_ID_MAX])
{
    AstraFontImportProgress progress = ASTRA_FONT_IMPORT_PROGRESS_INIT;
    id[0] = '\0';
    AstraResult result = ASTRA_OK;
    while (result == ASTRA_OK) {
        result = astra_fonts_import_state(job, &progress);
        if (result != ASTRA_OK) break;
        if (progress.state != ASTRA_FONT_IMPORT_RUNNING &&
            progress.state != ASTRA_FONT_IMPORT_CANCELLING) {
            result = progress.result;
            if (result == ASTRA_OK) memcpy(id, progress.installed_id, sizeof(progress.installed_id));
            break;
        }
        result = astra_result_from_syscall(astra_wait_one(
            astra_fonts_import_wait_handle(job), ASTRA_DEADLINE_FOREVER, NULL));
    }
    AstraResult closed = astra_fonts_import_close(job);
    return result == ASTRA_OK ? closed : result;
}

static AstraResult import_font(AstraHandle admin, const void *source,
    uint32_t length, const char *name, uint32_t name_length, uint32_t encoding,
    char id[ASTRA_FONT_ID_MAX])
{
    AstraFontImport job = ASTRA_FONT_IMPORT_INIT;
    id[0] = '\0';
    AstraResult result = astra_fonts_import_begin(admin, source, length, name,
                                                 name_length, encoding, &job);
    return result == ASTRA_OK ? finish_font_import(&job, id) : result;
}

#endif
