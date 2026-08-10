#ifndef ASTRA_SUPERVISOR_VFS_HOST_H
#define ASTRA_SUPERVISOR_VFS_HOST_H

#include <astra/vfs_assign.h>
#include <astra/vfs_client.h>

/*
 * Connects the protected storage service's published port, opens the
 * supervisor's session, and binds the standard assigns over it.
 */
int supervisor_vfs_start(uint32_t port_handle);

/* NULL until supervisor_vfs_start() has succeeded. */
AstraVfsClient *supervisor_vfs_client(void);

/* This process's namespace. NULL until supervisor_vfs_start() has succeeded. */
AstraAssignTable *supervisor_assigns(void);

/*
 * Which client speaks for an assign. A name is a binding to a mount, and a
 * second mount -- EVENTS: -- is a second service with a session of its own, so
 * a caller that resolved a path must ask what to send it through rather than
 * assume the storage one.
 *
 * Registering yields the handle an assign is bound with. It is not the
 * session: two services number their sessions independently and both start at
 * one, so a table matched by session hands every EVENTS: path to the volume --
 * which is a listing of the wrong filesystem rather than an error, and the
 * kind of bug that reads as "the tree is empty".
 *
 * The handle *is* the service's port send handle now, which is what makes a
 * child's namespace possible: the same number the shell routes on is the one a
 * launch hands over. It used to be a token this file invented, and a token
 * cannot be granted to anything.
 */
uint32_t supervisor_vfs_register(AstraVfsClient *client, uint32_t port_handle);
AstraVfsClient *supervisor_vfs_client_for(const AstraAssign *assign);

/* The send handle a launch grants for SYS:, WORK: and COMMANDS:. */
uint32_t supervisor_vfs_port(void);

/*
 * Stamps what this thread is doing onto every client, so a request carries the
 * story it belongs to whichever service it goes to. One call rather than one
 * per client: a client that was forgotten here is a request that leaves the
 * story silently.
 */
void supervisor_vfs_set_activity(uint32_t activity);

#endif
