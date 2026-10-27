#include <stdio.h>

#if __STDC_HOSTED__ != 1
#error POSIX programs must compile hosted
#endif

int main(void)
{
    return printf("Astra NDK\n") < 0;
}
