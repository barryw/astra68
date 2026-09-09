#ifndef ASTRA_POSIX_UNISTD_H
#define ASTRA_POSIX_UNISTD_H

#if defined(__GNUC__)
#pragma GCC system_header
#endif
#include_next <unistd.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* XSI saved-ID operations supplied by Astra's POSIX personality. */
int killpg(pid_t process_group, int signal_number);
int setresgid(gid_t real_gid, gid_t effective_gid, gid_t saved_gid);
int setresuid(uid_t real_uid, uid_t effective_uid, uid_t saved_uid);

#ifdef __cplusplus
}
#endif

#endif
