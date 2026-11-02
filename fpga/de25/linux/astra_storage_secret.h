#ifndef ASTRA_STORAGE_SECRET_H
#define ASTRA_STORAGE_SECRET_H

/* Root-only host key; return an open descriptor or -1 with errno. */
int astra_storage_secret_open(const char *path);

#endif
