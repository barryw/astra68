#ifndef ASTRAZ_STORY_PICKER_H
#define ASTRAZ_STORY_PICKER_H

#include <astra/runtime.h>
#include <astra/window.h>

/* Returns ASTRA_OK for both selection and cancellation; selected distinguishes them. */
AstraResult astraz_story_picker_open(const AstraStartupInfo *startup,
    AstraHandle gui, const AstraWindow *parent, const char *current,
    char *path, uint32_t capacity, int *selected);

#endif
