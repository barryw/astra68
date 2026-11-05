/* Check whether DE25 Linux can report exact mount-wide directory events. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fanotify.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fanotify.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    for (int at = 1; at < argc; ++at) {
        int fd = fanotify_init(FAN_CLASS_NOTIF | FAN_CLOEXEC | FAN_NONBLOCK |
                               FAN_REPORT_DFID_NAME_TARGET, O_RDONLY | O_LARGEFILE);
        if (fd < 0) {
            printf("%s init errno=%d\n", argv[at], errno);
            return 1;
        }
        int result = fanotify_mark(fd, FAN_MARK_ADD | FAN_MARK_FILESYSTEM,
                                   FAN_CREATE | FAN_DELETE | FAN_RENAME |
                                   FAN_MODIFY | FAN_ATTRIB, AT_FDCWD, argv[at]);
        if (result != 0) {
            printf("%s mark errno=%d\n", argv[at], errno);
            close(fd);
            return 1;
        }
        char directory[PATH_MAX], before[PATH_MAX] = "", after[PATH_MAX] = "";
        if (snprintf(directory, sizeof(directory), "%s/astra-fanotify.XXXXXX",
                     argv[at]) >= (int)sizeof(directory) || mkdtemp(directory) == NULL) {
            close(fd);
            return 1;
        }
        int success = 0;
        if (snprintf(before, sizeof(before), "%s/before", directory) >=
                (int)sizeof(before) ||
            snprintf(after, sizeof(after), "%s/after", directory) >=
                (int)sizeof(after)) goto cleanup;
        int file = open(before, O_CREAT | O_EXCL | O_RDWR, 0600);
        if (file < 0) goto cleanup;
        ssize_t written = write(file, "x", 1);
        int closed = close(file);
        if (written != 1 || closed != 0 || rename(before, after) != 0 ||
            unlink(after) != 0) goto cleanup;
        uint64_t seen = 0;
        unsigned targets = 0, parents = 0, old_names = 0, new_names = 0;
        for (int probe = 0; probe < 10 &&
             (seen & (FAN_CREATE | FAN_RENAME | FAN_DELETE)) !=
                 (FAN_CREATE | FAN_RENAME | FAN_DELETE); ++probe) {
            struct pollfd event = {fd, POLLIN, 0};
            if (poll(&event, 1, 200) <= 0) continue;
            char buffer[8192];
            ssize_t length = read(fd, buffer, sizeof(buffer));
            if (length < 0 && errno == EAGAIN) continue;
            if (length <= 0) goto cleanup;
            for (struct fanotify_event_metadata *meta = (void *)buffer;
                 FAN_EVENT_OK(meta, length); meta = FAN_EVENT_NEXT(meta, length)) {
                if (meta->pid != getpid()) continue;
                seen |= meta->mask;
                for (size_t offset = meta->metadata_len;
                     offset + sizeof(struct fanotify_event_info_header) <=
                         meta->event_len;) {
                    struct fanotify_event_info_header *info =
                        (void *)((char *)meta + offset);
                    if (info->len < sizeof(*info) || offset + info->len >
                        meta->event_len) goto cleanup;
                    if (info->info_type == FAN_EVENT_INFO_TYPE_FID) ++targets;
                    if (info->info_type == FAN_EVENT_INFO_TYPE_DFID_NAME) ++parents;
                    if (info->info_type == FAN_EVENT_INFO_TYPE_OLD_DFID_NAME) ++old_names;
                    if (info->info_type == FAN_EVENT_INFO_TYPE_NEW_DFID_NAME) ++new_names;
                    offset += info->len;
                }
            }
        }
        printf("%s masks=%llx target=%u parent=%u old=%u new=%u\n", argv[at],
               (unsigned long long)seen, targets, parents, old_names, new_names);
        success = (seen & (FAN_CREATE | FAN_RENAME | FAN_DELETE)) ==
                      (FAN_CREATE | FAN_RENAME | FAN_DELETE) &&
                  targets >= 3u && parents >= 2u && old_names == 1u &&
                  new_names == 1u;
cleanup:
        if (before[0] != '\0') (void)unlink(before);
        if (after[0] != '\0') (void)unlink(after);
        if (rmdir(directory) != 0) success = 0;
        close(fd);
        if (!success) return 1;
    }
    return 0;
}
