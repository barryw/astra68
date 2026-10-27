#ifndef ASTRA_BUNDLE_INTERNAL_H
#define ASTRA_BUNDLE_INTERNAL_H

/* Shared bundle layout and provider policy for native and host consumers. */
#include <stdint.h>
#include <astra/bundle.h>

int astra_bundle_library_payload_path(const char *root, uint16_t kind,
                                      const AstraBundleLibrary *library,
                                      char *out, uint32_t capacity);
const AstraBundleLibrary *astra_bundle_private_provider(
    const AstraBundleManifest *manifest, const char *name, uint16_t abi);
int astra_bundle_library_satisfies(const AstraBundleLibrary *requirement,
                                   const AstraBundleLibrary *provider);

#endif
