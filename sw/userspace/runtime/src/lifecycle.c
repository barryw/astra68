#include <stddef.h>

typedef void (*AstraLifecycleFunction)(void);

extern AstraLifecycleFunction __preinit_array_start[]
    __attribute__((weak));
extern AstraLifecycleFunction __preinit_array_end[]
    __attribute__((weak));
extern AstraLifecycleFunction __init_array_start[]
    __attribute__((weak));
extern AstraLifecycleFunction __init_array_end[]
    __attribute__((weak));
extern AstraLifecycleFunction __fini_array_start[]
    __attribute__((weak));
extern AstraLifecycleFunction __fini_array_end[]
    __attribute__((weak));

void
astra_runtime_call_array(AstraLifecycleFunction *begin,
                         AstraLifecycleFunction *end, int reverse)
{
    ptrdiff_t count;

    if (begin == NULL || end == NULL || end < begin)
        return;
    count = end - begin;
    if (reverse) {
        while (count != 0)
            begin[--count]();
    } else {
        for (ptrdiff_t i = 0; i < count; ++i)
            begin[i]();
    }
}

void
astra_runtime_initialize(void)
{
    astra_runtime_call_array(__preinit_array_start, __preinit_array_end, 0);
    astra_runtime_call_array(__init_array_start, __init_array_end, 0);
}

int
astra_runtime_finalize(int status)
{
    astra_runtime_call_array(__fini_array_start, __fini_array_end, 1);
    return status;
}
