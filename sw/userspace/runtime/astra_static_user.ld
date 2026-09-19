/*
 * Explicit bootstrap/recovery link contract. Reusable system implementations
 * belong in .library files; select this script only when the image must run
 * before or without the dynamic loader.
 */
INCLUDE astra_user_contract.ld
ASTRA_DYNAMIC_IMAGE = 0;

PHDRS
{
    text PT_LOAD FILEHDR PHDRS FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
    interp PT_NULL FLAGS(0);
    dynamic PT_NULL FLAGS(0);
    tls PT_TLS FLAGS(4);
    stack PT_GNU_STACK FLAGS(6);
}

INCLUDE astra_user_sections.ld
