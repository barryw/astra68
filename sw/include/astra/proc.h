#ifndef ASTRA_PROC_H
#define ASTRA_PROC_H

#include <astra/process.h>

#define ASTRA_PROC_NAME_MAX 32u

typedef struct AstraProcSnapshot {
    AstraProcessInfo process;
    char name[ASTRA_PROC_NAME_MAX];
} AstraProcSnapshot;

_Static_assert(sizeof(AstraProcSnapshot) == 112u,
               "PROC snapshot record ABI changed");

#endif
