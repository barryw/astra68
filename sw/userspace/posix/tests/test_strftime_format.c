#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int __d_snprintf(char *bytes, size_t capacity, const char *format, ...);

int
main(void)
{
    char out[32];
    char short_out[5];

    assert(__d_snprintf(out, sizeof(out), "%s %.*d:%02d %lld", "UTC", 3,
                        7, 4, INT64_C(-42)) == 14);
    assert(strcmp(out, "UTC 007:04 -42") == 0);
    assert(__d_snprintf(short_out, sizeof(short_out), "%u", 123456u) == 6);
    assert(strcmp(short_out, "1234") == 0);
    puts("astra posix strftime format: PASS");
    return 0;
}
