#include "astra_font_worker.h"

#include <unistd.h>

int main(void)
{
    return astra_font_worker_run(STDIN_FILENO, STDOUT_FILENO);
}
