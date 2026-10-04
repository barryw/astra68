/* Keep upstream unchanged; native services use the canonical runtime heap. */
#include <stdlib.h>
#include <astra/runtime.h>
#define malloc astra_runtime_allocate
#define calloc astra_runtime_callocate
#define free astra_runtime_deallocate
