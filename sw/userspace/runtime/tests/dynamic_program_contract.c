#include <astra/program.h>

ASTRA_PROGRAM("dynamic-contract", 1, 0, 0, "Astra68 contributors",
              "Copyright 2026 Astra68 contributors");

extern int astra_dynamic_contract_add(int left, int right);
static volatile int result;

static void dynamic_contract_preinitialize(void)
{
    result = -1;
}

typedef void (*DynamicContractCallback)(void);
static DynamicContractCallback const dynamic_contract_preinitializer
    __attribute__((used, section(".preinit_array"))) =
        dynamic_contract_preinitialize;

void _start(void)
{
    result = astra_dynamic_contract_add(19, 23);
}
