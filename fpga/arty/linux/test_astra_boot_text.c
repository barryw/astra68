// SPDX-License-Identifier: MIT

#include "astra_boot_text.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    struct astra_boot_text_line line;
    unsigned column;

    astra_boot_text_line_raw(&line, "A\x01", ASTRA_BOOT_RED);
    assert(line.cell[0] == 0x0341u);
    assert(line.cell[1] == 0x033fu);
    for (column = 2; column < ASTRA_BOOT_TEXT_COLS; ++column)
        assert(line.cell[column] == 0x0020u);

    astra_boot_text_line_stage(&line, "Linux host", "OK",
                               ASTRA_BOOT_AMBER);
    assert(line.cell[0] == 0x013eu);
    assert(line.cell[1] == 0x0020u);
    assert(line.cell[2] == 0x004cu);
    assert(line.cell[12] == 0x002eu);
    assert(line.cell[34] == 0x014fu);
    assert(line.cell[35] == 0x014bu);

    puts("ASTRA BOOT TEXT SOFTWARE PASS");
    return 0;
}
