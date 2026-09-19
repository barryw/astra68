/*
 * Normal Astra program link contract: eager dynamic dependencies interpreted
 * by Astra's versioned loader library. The loader resolves and binds the
 * complete closure before application startup executes.
 */
INCLUDE astra_user_contract.ld
ASTRA_DYNAMIC_IMAGE = 1;

PHDRS
{
    text PT_LOAD FILEHDR PHDRS FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
    interp PT_INTERP FLAGS(4);
    dynamic PT_DYNAMIC FLAGS(6);
    tls PT_TLS FLAGS(4);
    relro PT_GNU_RELRO;
    stack PT_GNU_STACK FLAGS(6);
}

INCLUDE astra_user_sections.ld
