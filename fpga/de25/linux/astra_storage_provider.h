#ifndef ASTRA_STORAGE_PROVIDER_LINUX_H
#define ASTRA_STORAGE_PROVIDER_LINUX_H

#include "astra_storage_catalog.h"
#include "astra_storage_mount.h"
#include "astra_storage_path.h"
#include "astra_storage_probe.h"
#include "astra_storage_work.h"
#include <signal.h>

typedef struct AstraStorageResource AstraStorageResource;
struct AstraStorageDiscovery;
typedef struct AstraStorageProvider {
    AstraStorageCatalog catalog;
    AstraStorageDomain domain;
    AstraStorageResource *resources;
    size_t resource_bytes;
    size_t resource_limit;
    uint64_t ram_bytes;
    uint64_t ram_inodes;
    uint64_t ram_byte_limit;
    uint64_t ram_inode_limit;
    uint32_t probes;
    uint32_t probe_limit;
    uint32_t probe_timeout_ms;
    uint32_t workers;
    uint32_t worker_limit;
    int lease_signal;
    sigset_t previous_signals;
    uint64_t epoch;
    uint64_t system_source;
    struct AstraStorageDiscovery *discovery;
} AstraStorageProvider;

#define ASTRA_STORAGE_PROVIDER_INIT { .domain = {.directory = -1}, .lease_signal = -1 }

/* Enter the private mount namespace and consume the precreated domain only
 * after successful admission. The caller retains it on every failure. */
int astra_storage_provider_open(AstraStorageProvider *provider, AstraStorageDomain *domain,
                                 size_t metadata_limit, size_t resource_limit,
                                 uint32_t probes, uint32_t timeout_ms, uint32_t workers,
                                 uint64_t ram_bytes, uint64_t ram_inodes);
/* Refuses while mounts, probe children, or source loans remain. */
int astra_storage_provider_close(AstraStorageProvider *provider);
/* Privileged launcher/discovery only. No application path reaches these calls. */
int astra_storage_provider_export(AstraStorageProvider *provider, int root,
                                   const char *name, uint32_t flags, uint64_t *id);
/* Zero accepts a charged source into the FIFO; poll reports its probe result. */
int astra_storage_provider_source(AstraStorageProvider *provider, uint64_t source,
                                   uint64_t generation, int fd,
                                   uint64_t offset, uint64_t length);
int astra_storage_provider_image(AstraStorageProvider *provider, uint64_t mount,
                                  uint64_t generation, uint64_t backing_object,
                                  int file, uint64_t *source);
/* Opened exact trusted image/window; writable lease excludes every other
 * opener before probing through the prepared block window. */
int astra_storage_provider_system(AstraStorageProvider *provider, int file,
                                   uint64_t offset, uint64_t length, uint64_t *source);
/* Called after peer exit; retains and reports real pending cleanup. */
int astra_storage_provider_shutdown(AstraStorageProvider *provider);
/* Drain only retired observations while other volumes remain live. Cleanup
 * uses the same real unmount/release path as final shutdown. */
int astra_storage_provider_reap(AstraStorageProvider *provider);
int astra_storage_provider_poll(AstraStorageProvider *provider);
/* Wait for an actual child/lease event or the nearest probe deadline. */
int astra_storage_provider_wait(AstraStorageProvider *provider, int timeout_ms);
int astra_storage_provider_mount(AstraStorageProvider *provider, uint64_t source,
                                  uint64_t generation, uint32_t flags,
                                  uint64_t bytes, uint64_t inodes, uint64_t *mount);
/* Mount/unmount return admission status; poll commits their real outcome to
 * the catalog. MOUNTING/QUIESCING remain inspectable while Linux is working. */
/* Caller has quiesced canonical QEMU references. Linux still refuses a real
 * busy mount. Source/image dependencies are independently checked here. */
int astra_storage_provider_unmount(AstraStorageProvider *provider, uint64_t mount,
                                    uint64_t generation);
int astra_storage_provider_eject(AstraStorageProvider *provider, uint64_t source,
    uint64_t generation);
/* Only provider's QEMU peer may request a format, after native admin checks.
 * Admission is asynchronous; query the exact prior generation for result. */
int astra_storage_provider_format(AstraStorageProvider *provider, uint64_t source,
    uint64_t generation, uint32_t format, const char *label);
int astra_storage_provider_format_status(AstraStorageProvider *provider,
    uint64_t source, uint64_t generation, int *result);
/* Provider resource half of parent-disk edit admission. Catalog checks
 * mounts and policy; this checks children that have not finished work yet. */
int astra_storage_provider_device_idle(AstraStorageProvider *provider, uint64_t device);
/* Exact-device partition edit; catalog and live-resource admission fence all
 * descendants before the worker changes the retained physical disk. */
int astra_storage_provider_partition(AstraStorageProvider *provider,
    uint64_t device, uint64_t generation, const AstraVolumePartitionEdit *edit);
int astra_storage_provider_partition_status(AstraStorageProvider *provider,
    uint64_t device, uint64_t generation, int *result);
/* Retire an admitted ISO/UDF image source only after dependent mounts are gone.
 * Cancellation remains charged until its actual child and lease retire. */
int astra_storage_provider_release_image(AstraStorageProvider *provider, uint64_t source,
    uint64_t generation);
/* Private short-lived loan for QEMU's canonical native usage inspection. */
int astra_storage_provider_backing(AstraStorageProvider *provider, uint64_t source,
    uint64_t generation, int *file);
int astra_storage_provider_root(AstraStorageProvider *provider, uint64_t id,
                                 uint64_t generation, int *root);
int astra_storage_provider_path(AstraStorageProvider *provider, int socket,
                                 pid_t peer, uid_t uid, gid_t gid);
int astra_storage_provider_control(AstraStorageProvider *provider, int socket,
                                    pid_t peer, uid_t uid, gid_t gid);
/* Serve the inherited peer endpoints, wake for actual child exit/lease breaks,
 * and enforce probe deadlines without polling a cancelled uninterruptible job. */
int astra_storage_provider_serve(AstraStorageProvider *provider, int control, int path,
                                  pid_t peer);
/* Retire observation, cancel probes; cleanup retains failed mounts/loop charges. */
int astra_storage_provider_loss(AstraStorageProvider *provider, uint64_t id,
                                 uint64_t generation);
int astra_storage_provider_release(AstraStorageProvider *provider, uint64_t id);

#endif
