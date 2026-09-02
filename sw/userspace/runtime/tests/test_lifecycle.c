#include <assert.h>
#include <stddef.h>
#include <stdio.h>

typedef void (*AstraLifecycleFunction)(void);

void astra_runtime_call_array(AstraLifecycleFunction *begin,
                              AstraLifecycleFunction *end, int reverse);

static int calls[3];
static int count;

static void first(void) { calls[count++] = 1; }
static void second(void) { calls[count++] = 2; }
static void third(void) { calls[count++] = 3; }

int
main(void)
{
    AstraLifecycleFunction functions[] = {first, second, third};

    astra_runtime_call_array(functions, functions + 3, 0);
    assert(count == 3 && calls[0] == 1 && calls[1] == 2 && calls[2] == 3);
    count = 0;
    astra_runtime_call_array(functions, functions + 3, 1);
    assert(count == 3 && calls[0] == 3 && calls[1] == 2 && calls[2] == 1);
    count = 0;
    astra_runtime_call_array(functions + 3, functions, 1);
    assert(count == 0);
    astra_runtime_call_array(NULL, functions, 0);
    puts("runtime lifecycle: PASS");
    return 0;
}
