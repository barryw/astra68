#include "volume_cli.h"
#include <astra/posix.h>
#include <astra/runtime.h>
#include <astra/status.h>
#include <stdio.h>
#include <string.h>

int volume_cli_open(VolumeCli *cli, const char *command)
{
    memset(cli, 0, sizeof(*cli));
    const AstraStartupInfo *startup = astra_posix_startup();
    const AstraStartupCapability *metadata = astra_startup_capability(startup, ASTRA_CAPABILITY_VOLUMES);
    const AstraStartupCapability *administrator = astra_startup_capability(startup, ASTRA_CAPABILITY_VOLUME_ADMIN);
    if (metadata == NULL || administrator == NULL) {
        fprintf(stderr, "%s: volume metadata or administrator capability unavailable\n", command);
        return 0;
    }
    cli->metadata = metadata->handle;
    cli->administrator = administrator->handle;
    AstraResult result = astra_volume_watch_open(cli->metadata, &cli->watch);
    if (result != ASTRA_OK) {
        fprintf(stderr, "%s: cannot observe lifecycle (%ld)\n", command, (long)result);
        return 0;
    }
    return 1;
}

void volume_cli_close(VolumeCli *cli)
{
    AstraResult result = astra_volume_watch_close(cli->metadata, &cli->watch);
    (void)result;
}

int volume_cli_number(const char *text, uint64_t *number)
{
    uint64_t value = 0u;
    if (text == NULL || *text == '\0') return 0;
    for (; *text != '\0'; ++text) {
        if (*text < '0' || *text > '9') return 0;
        uint32_t digit = (uint32_t)(*text - '0');
        if (value > (UINT64_MAX - digit) / 10u) return 0;
        value = value * 10u + digit;
    }
    if (value == 0u) return 0;
    *number = value;
    return 1;
}

int volume_cli_print_bytes(const char *text, uint32_t length)
{
    static const char hex[] = "0123456789abcdef";
    for (uint32_t at = 0u; at < length; ++at) {
        unsigned char byte = (unsigned char)text[at];
        if (byte < 32u || byte == 127u) {
            if (fputs("\\x", stdout) == EOF || putchar(hex[byte >> 4u]) == EOF ||
                putchar(hex[byte & 15u]) == EOF) return 0;
        } else if (byte == '\\') {
            if (fputs("\\\\", stdout) == EOF) return 0;
        } else if (putchar(byte) == EOF) return 0;
    }
    return 1;
}

static int volume_name_equal(const char *selector, const char *name)
{
    if (*selector == '/') ++selector;
    if (*name == '/') ++name;
    if (*selector == '\0') return 0;
    for (; *selector != '\0' && *name != '\0'; ++selector, ++name) {
        unsigned char left = (unsigned char)*selector, right = (unsigned char)*name;
        if (left == '/') return 0;
        if (left >= 'A' && left <= 'Z') left = (unsigned char)(left - 'A' + 'a');
        if (right >= 'A' && right <= 'Z') right = (unsigned char)(right - 'A' + 'a');
        if (left != right) return 0;
    }
    return *selector == '\0' && *name == '\0';
}

int volume_cli_select(VolumeCli *cli, const char *selector, uint32_t kind,
    AstraVolumeRecord *record, AstraVolumeReference *reference,
    AstraVolumeReference *parent)
{
    AstraVolumeSnapshot snapshot = ASTRA_VOLUME_SNAPSHOT_INIT;
    uint64_t id = 0u;
    int numeric = volume_cli_number(selector, &id);
    AstraResult result = astra_volume_snapshot_open(cli->metadata, &snapshot);
    if (result != ASTRA_OK) {
        fprintf(stderr, "volume: catalog unavailable (%ld)\n", (long)result);
        return 0;
    }
    int found = 0;
    for (uint32_t at = 0u; at < snapshot.count; ++at) {
        const AstraVolumeRecord *row = &snapshot.records[at];
        int eligible = kind == 0u ? row->kind == ASTRA_VOLUME_MOUNT || row->kind == ASTRA_VOLUME_BINDING : row->kind == kind;
        int named_current = row->state == ASTRA_VOLUME_MOUNTED ||
            row->state == ASTRA_VOLUME_MOUNTING || row->state == ASTRA_VOLUME_QUIESCING;
        if (eligible && (numeric ? row->id == id : named_current && volume_name_equal(selector, row->name))) {
            *record = *row;
            *reference = (AstraVolumeReference){snapshot.epoch, row->id, row->generation};
            if (parent != NULL)
                *parent = (AstraVolumeReference){snapshot.epoch, row->parent, row->parent_generation};
            found = 1;
            break;
        }
    }
    astra_volume_snapshot_close(&snapshot);
    if (!found) fprintf(stderr, "volume: no current %s '%s'\n",
        kind == ASTRA_VOLUME_SOURCE ? "source" : kind == 0u ? "volume" : "mount", selector);
    return found;
}

static int wait_lifecycle(VolumeCli *cli, const AstraVolumeReference *reference,
    uint32_t wanted, int disappearance_completes, AstraVolumeRecord *record,
    AstraVolumeRemoval *removal)
{
    uint64_t deadline = astra_clock_monotonic() + UINT64_C(60000000000);
    for (;;) {
        AstraVolumeChange change;
        AstraResult result;
        while ((result = astra_volume_watch_receive(&cli->watch, &change)) == ASTRA_OK) {}
        if (result != ASTRA_ERROR_WOULD_BLOCK) {
            fprintf(stderr, "volume: lifecycle observation failed (%ld)\n", (long)result);
            return 0;
        }
        AstraVolumeSnapshot snapshot = ASTRA_VOLUME_SNAPSHOT_INIT;
        result = astra_volume_snapshot_open(cli->metadata, &snapshot);
        if (result == ASTRA_OK) {
            if (removal != NULL) {
                result = astra_volume_removal_advance(cli->administrator, &snapshot, removal);
                astra_volume_snapshot_close(&snapshot);
                if (result != ASTRA_OK) {
                    fprintf(stderr, "volume: removal observation failed (%ld)\n", (long)result);
                    return 0;
                }
                if (removal->state == ASTRA_VOLUME_REMOVAL_COMPLETE) return 1;
                if (removal->state == ASTRA_VOLUME_REMOVAL_PROVIDER_RETIRED) {
                    fputs("volume: provider incarnation retired during operation\n", stderr);
                    return 0;
                }
                if (removal->state == ASTRA_VOLUME_REMOVAL_REFUSED ||
                    removal->state == ASTRA_VOLUME_REMOVAL_RETAINED) {
                    fprintf(stderr, "volume: %s (status %u)\n",
                        removal->state == ASTRA_VOLUME_REMOVAL_REFUSED ?
                            "unmount refused; volume remains mounted" :
                            "removal incomplete; storage resources remain retained",
                        removal->status);
                    return 0;
                }
            } else {
                int found = 0;
                if (snapshot.epoch == reference->epoch)
                    for (uint32_t at = 0u; at < snapshot.count; ++at) {
                        const AstraVolumeRecord *row = &snapshot.records[at];
                        if (row->id == reference->id) {
                            *record = *row;
                            found = 1;
                            break;
                        }
                    }
                int complete = found ? wanted != 0u && record->generation == reference->generation &&
                    record->state == wanted : disappearance_completes;
                int failed = found && (record->state == ASTRA_VOLUME_FAILED ||
                    (!disappearance_completes && (record->state == ASTRA_VOLUME_LOST ||
                        record->generation != reference->generation)) ||
                    (wanted == ASTRA_VOLUME_UNMOUNTED && record->state == ASTRA_VOLUME_MOUNTED) ||
                    (wanted == 0u && record->generation == reference->generation &&
                        record->state == ASTRA_VOLUME_UNMOUNTED && record->status != ASTRA_STATUS_OK));
                int epoch_lost = snapshot.epoch != reference->epoch;
                astra_volume_snapshot_close(&snapshot);
                if (epoch_lost) {
                    fputs("volume: provider incarnation retired during operation\n", stderr);
                    return 0;
                }
                if (complete) return 1;
                if (!found || failed) {
                    fprintf(stderr, "volume: lifecycle failed (status %u, state %u)\n",
                        found ? record->status : ASTRA_STATUS_LOST, found ? record->state : ASTRA_VOLUME_LOST);
                    return 0;
                }
            }
        } else if (result != ASTRA_ERROR_BUSY) {
            fprintf(stderr, "volume: catalog failed after admission (%ld)\n", (long)result);
            return 0;
        }
        uint64_t now = astra_clock_monotonic();
        if (now >= deadline) {
            fprintf(stderr, "volume: operation still pending for %llu:%llu:%llu; inspect volumes --all\n",
                (unsigned long long)reference->epoch, (unsigned long long)reference->id,
                (unsigned long long)reference->generation);
            return 0;
        }
        uint64_t next = now + UINT64_C(100000000);
        if (next > deadline) next = deadline;
        uint32_t waited = astra_wait_one(cli->watch.receive, next, NULL);
        if (waited != ASTRA_SYSCALL_OK && waited != ASTRA_SYSCALL_TIMED_OUT && waited != ASTRA_SYSCALL_CANCELLED) {
            fprintf(stderr, "volume: notification wait failed (%u)\n", waited);
            return 0;
        }
    }
}

int volume_cli_wait(VolumeCli *cli, const AstraVolumeReference *reference,
    uint32_t wanted, int disappearance_completes, AstraVolumeRecord *record)
{
    return wait_lifecycle(cli, reference, wanted, disappearance_completes, record, NULL);
}

int volume_cli_wait_removal(VolumeCli *cli, AstraVolumeRemoval *removal)
{
    return wait_lifecycle(cli, &removal->mount, 0u, 0, NULL, removal);
}

int volume_cli_apply(VolumeCli *cli, const AstraVolumeControl *control,
    AstraVolumeReference *reference)
{
    AstraResult result = astra_volume_control(cli->administrator, control, reference);
    if (result != ASTRA_OK) {
        if (result == ASTRA_ERROR_BUSY) {
            const AstraStartupCapability *usage = astra_startup_capability(
                astra_posix_startup(), ASTRA_CAPABILITY_VOLUME_USAGE);
            AstraVolumeUsageSnapshot snapshot = ASTRA_VOLUME_USAGE_SNAPSHOT_INIT;
            int mount = control->operation == ASTRA_VOLUME_UNMOUNT;
            AstraResult inspected = usage == NULL ? ASTRA_ERROR_PERMISSION :
                astra_volume_usage_snapshot_open(usage->handle, mount ? &control->reference : NULL, &snapshot);
            AstraVolumeSnapshot catalog = ASTRA_VOLUME_SNAPSHOT_INIT;
            if (inspected == ASTRA_OK && !mount) {
                inspected = astra_volume_snapshot_open(cli->metadata, &catalog);
                if (inspected == ASTRA_OK && (catalog.sequence != snapshot.catalog_sequence ||
                    catalog.epoch != control->reference.epoch)) inspected = ASTRA_ERROR_BUSY;
            }
            if (inspected == ASTRA_OK) {
                fputs("volume: retained holders prevent removal:\n", stderr);
                uint32_t shown = 0u;
                for (uint32_t at = 0u; at < snapshot.count; ++at) {
                    const AstraVolumeUsageRecord *row = &snapshot.records[at];
                    int selected = mount || (row->kind == ASTRA_VOLUME_USAGE_IMAGE &&
                        row->handle == control->reference.id);
                    if (!selected)
                        for (uint32_t item = 0u; item < catalog.count; ++item)
                            if (catalog.records[item].id == row->volume.id &&
                                catalog.records[item].generation == row->volume.generation &&
                                catalog.records[item].parent == control->reference.id &&
                                catalog.records[item].parent_generation == control->reference.generation)
                                selected = 1;
                    if (!selected) continue;
                    ++shown;
                    fprintf(stderr, "  PID %u type %u handle %llu inode %llu %s%s ",
                        row->process, row->kind, (unsigned long long)row->handle,
                        (unsigned long long)row->inode,
                        (row->rights & ASTRA_RIGHT_READ) != 0u ? "read" : "",
                        (row->rights & ASTRA_RIGHT_WRITE) != 0u ? "/write" : "");
                    for (uint32_t byte = 0u; byte < row->path_length; ++byte) {
                        unsigned char value = (unsigned char)row->path[byte];
                        if (value < 32u || value == 127u) fprintf(stderr, "\\x%02x", value);
                        else fputc(value, stderr);
                    }
                    if (row->flags & ASTRA_VOLUME_USAGE_UNNAMED) fputs("[unnamed retained object]", stderr);
                    if (row->flags & ASTRA_VOLUME_USAGE_LOST) fputs(" [lost]", stderr);
                    if (row->flags & ASTRA_VOLUME_USAGE_ACTIVE) fputs(" [operation active]", stderr);
                    if (row->flags & ASTRA_VOLUME_USAGE_CLEANUP_PENDING) fputs(" [cleanup pending]", stderr);
                    fputc('\n', stderr);
                }
                if (shown == 0u)
                    fputs("  Previous operation finished; no holders remain in this snapshot. Retry removal.\n", stderr);
            } else fprintf(stderr, "volume: holder inspection failed (%ld)\n", (long)inspected);
            astra_volume_usage_snapshot_close(&snapshot);
            astra_volume_snapshot_close(&catalog);
            return 0;
        }
        fprintf(stderr, "volume: lifecycle admission failed (%ld)\n", (long)result);
        return 0;
    }
    return 1;
}

int volume_cli_mount(VolumeCli *cli, const AstraVolumeReference *source,
    uint32_t flags, uint64_t bytes, uint64_t inodes)
{
    AstraVolumeControl control = {.reference = *source, .operation = ASTRA_VOLUME_MOUNT_SOURCE,
        .flags = flags, .byte_limit = bytes, .inode_limit = inodes};
    AstraVolumeReference mounted;
    AstraVolumeRecord record = {0};
    if (!volume_cli_apply(cli, &control, &mounted) ||
        !volume_cli_wait(cli, &mounted, ASTRA_VOLUME_MOUNTED, 0, &record)) return 0;
    return printf("mounted %s (%llu:%llu:%llu)%s\n", record.name,
        (unsigned long long)mounted.epoch, (unsigned long long)mounted.id,
        (unsigned long long)mounted.generation,
        (record.flags & ASTRA_VOLUME_READ_ONLY) != 0u ? " read-only" : " writable") >= 0;
}
