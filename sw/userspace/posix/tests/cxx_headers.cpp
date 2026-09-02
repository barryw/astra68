#include <astra/program.h>
#include <astra/runtime.h>
#include <pthread.h>

static_assert(sizeof(AstraProgram) == ASTRA_PROGRAM_SIZE);
static_assert(sizeof(AstraSyscallResult) == 20u);

int astra_cxx_headers_contract()
{
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    return pthread_mutex_destroy(&mutex);
}
