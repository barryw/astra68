#include "frotz_stream_word.h"

#include <assert.h>

int main(void)
{
    const uint16_t plain[] = {'R', 'o', 'o', 'm', 0u};
    const uint16_t style_zero[] = {1u, 0u, 'B', 'a', 't', 'h', 0u};
    const uint16_t font_zero[] = {2u, 0u, 'R', 'o', 'o', 'm', 0u};
    const uint16_t both_zero[] = {1u, 0u, 2u, 0u, 'X', 0u};
    const uint16_t terminator[] = {1u, 0u, 'B', 0u, 'X', 0u};
    assert(astraz_frotz_stream_word_length(plain) == 4u);
    assert(astraz_frotz_stream_word_length(style_zero) == 6u);
    assert(astraz_frotz_stream_word_length(font_zero) == 6u);
    assert(astraz_frotz_stream_word_length(both_zero) == 5u);
    assert(astraz_frotz_stream_word_length(terminator) == 3u);
    return 0;
}
