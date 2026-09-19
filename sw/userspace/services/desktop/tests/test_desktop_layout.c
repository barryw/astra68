#include "../desktop_layout.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(ASTRA_DESKTOP_LABEL_FONT_HEIGHT == 13u);
    assert(ASTRA_DESKTOP_LABEL_FONT_HEIGHT >
           ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT);
    assert(ASTRA_DESKTOP_ICON_X == 40u);
    assert(astra_desktop_centered_label_x(48u) == 48);
    assert(astra_desktop_centered_label_x(80u) == 32);
    assert(astra_desktop_centered_label_x(96u) == 32);
    puts("desktop layout tests passed");
    return 0;
}
