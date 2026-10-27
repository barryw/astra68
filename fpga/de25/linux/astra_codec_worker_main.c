#include <unistd.h>
#include "astra_codec_sandbox.h"

int astra_codec_worker_run(int socket);

int main(void)
{
    return astra_codec_sandbox_enter() == 0 ?
        astra_codec_worker_run(STDIN_FILENO) : 1;
}
