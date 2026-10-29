#ifndef ASTRAZ_LINE_INPUT_H
#define ASTRAZ_LINE_INPUT_H

#include <astra/text_model.h>

enum {
    ASTRAZ_EDIT_INSERT = 1, ASTRAZ_EDIT_BACKSPACE,
    ASTRAZ_EDIT_DELETE, ASTRAZ_EDIT_LEFT, ASTRAZ_EDIT_RIGHT,
    ASTRAZ_EDIT_HOME, ASTRAZ_EDIT_END
};

/* Shared TextModel supplies storage, UTF-8 boundaries, and selection. */
AstraResult astraz_line_input_edit(AstraTextModel *model, uint32_t operation,
    uint32_t scalar, uint32_t maximum_scalars);

#endif
