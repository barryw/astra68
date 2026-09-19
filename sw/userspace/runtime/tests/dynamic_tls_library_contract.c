__attribute__((visibility("default")))
_Thread_local int astra_dynamic_contract_tls = 7;

static _Thread_local volatile int local_tls = 11;

__attribute__((visibility("default")))
int astra_dynamic_contract_read_tls(void)
{
    return astra_dynamic_contract_tls;
}

__attribute__((visibility("default")))
int astra_dynamic_contract_read_local_tls(void)
{
    return local_tls;
}
