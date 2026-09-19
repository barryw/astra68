static volatile int lifecycle_state;

static void __attribute__((constructor)) dynamic_contract_initialize(void)
{
    lifecycle_state = 1;
}

static void __attribute__((destructor)) dynamic_contract_finalize(void)
{
    lifecycle_state = 0;
}

__attribute__((visibility("default")))
int astra_dynamic_contract_add(int left, int right)
{
    return left + right;
}
