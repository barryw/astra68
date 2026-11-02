#ifndef ASTRA_STORAGE_CONFIG_H
#define ASTRA_STORAGE_CONFIG_H

/* Move the guest's legacy /config tree behind the private host export. */
int astra_storage_config_migrate(int system_root, int private_root);

#endif
