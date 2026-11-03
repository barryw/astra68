#ifndef ASTRA_VOLUME_NETWORK_H
#define ASTRA_VOLUME_NETWORK_H

#include <astra/volume_abi.h>
#include <astra/utf8.h>
#include <stddef.h>

static inline int astra_volume_network_source_valid(const AstraVolumeNetworkSource *source)
{
    if (source == NULL || (source->format != ASTRA_VOLUME_NFS &&
        source->format != ASTRA_VOLUME_SMB) || source->reserved != 0u)
        return 0;
    size_t server = 0u, share = 0u, label = 0u;
    while (server < sizeof(source->server) && source->server[server] != 0) ++server;
    while (share < sizeof(source->share) && source->share[share] != 0) ++share;
    while (label < sizeof(source->label) && source->label[label] != 0) ++label;
    if (server == 0u || server == sizeof(source->server) ||
        share == 0u || share == sizeof(source->share) ||
        label == sizeof(source->label) || source->share[0] != '/' ||
        (source->format == ASTRA_VOLUME_SMB && share < 2u) ||
        !astra_utf8_validate(source->share, (uint32_t)share, 0u) ||
        !astra_utf8_validate(source->label, (uint32_t)label, 0u)) return 0;
    for (size_t at = 0u; at < sizeof(source->server); ++at) {
        unsigned char c = (unsigned char)source->server[at];
        if (at >= server) { if (c != 0u) return 0; }
        else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                   (c >= '0' && c <= '9') || c == '.' || c == '-' || c == ':')) return 0;
    }
    for (size_t at = 0u; at < sizeof(source->share); ++at) {
        unsigned char c = (unsigned char)source->share[at];
        if (at >= share) { if (c != 0u) return 0; }
        else if (c < 0x20u || c == 0x7fu || c == ':' ||
                 (source->format == ASTRA_VOLUME_SMB &&
                  (c == '\\' || c == ',' || c == '=' ||
                   (at != 0u && c == '/')))) return 0;
    }
    for (size_t at = 0u; at < sizeof(source->label); ++at) {
        unsigned char c = (unsigned char)source->label[at];
        if (at >= label) { if (c != 0u) return 0; }
        else if (c < 0x20u || c == 0x7fu) return 0;
    }
    return 1;
}

static inline int astra_volume_auth_field_valid(const char *text,
    size_t capacity, int required, int reject_controls)
{
    size_t length = 0u;
    while (length < capacity && text[length] != '\0') ++length;
    if (length == capacity || (required && length == 0u) ||
        !astra_utf8_validate(text, (uint32_t)length, 0u)) return 0;
    for (size_t at = 0u; at < capacity; ++at) {
        unsigned char c = (unsigned char)text[at];
        if (at >= length) { if (c != 0u) return 0; }
        else if (reject_controls && (c < 0x20u || c == 0x7fu)) return 0;
    }
    return 1;
}

static inline int astra_volume_mount_auth_valid(const AstraVolumeMountAuth *auth)
{
    return auth != NULL && auth->source.epoch != 0u && auth->source.id != 0u &&
        auth->source.generation != 0u &&
        (auth->flags & ~ASTRA_VOLUME_READ_ONLY) == 0u && auth->reserved == 0u &&
        astra_volume_auth_field_valid(auth->account, sizeof(auth->account), 1, 1) &&
        astra_volume_auth_field_valid(auth->domain, sizeof(auth->domain), 0, 1) &&
        astra_volume_auth_field_valid(auth->password, sizeof(auth->password), 0, 0);
}

#endif
