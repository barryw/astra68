#include <astra/font_store.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    char token[ASTRA_FONT_OBJECT_TOKEN_SIZE];

    astra_font_object_token("abc", 3u, token);
    assert(strcmp(token,
                  "xj4bnp4pahh6uqkbidpf3lrceoyagyndsylxvhfucd7wd4qacwwq") == 0);
    assert(astra_font_object_token_valid(token, 52u));
    token[51] = 'b';
    assert(!astra_font_object_token_valid(token, 52u));
    token[51] = 'q';
    token[0] = 'X';
    assert(!astra_font_object_token_valid(token, 52u));
    token[0] = 'x';
    assert(!astra_font_object_token_valid(token, 51u));
    assert(!astra_font_object_token_valid(NULL, 52u));
    puts("font object store passed");
    return 0;
}
