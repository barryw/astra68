#ifndef ASTRA_VFS_PORT_RECEIVE_H
#define ASTRA_VFS_PORT_RECEIVE_H

#include <astra/runtime.h>

/* Import pressure leaves the message queued; reclaim owners before one retry. */
static inline uint32_t astra_vfs_port_receive_reclaim(uint32_t receive,
    void *message, uint32_t capacity, uint32_t *handles,
    uint32_t handle_capacity, uint32_t *size, uint32_t *count,
    uint32_t *sender, void (*reclaim)(void *), void *context)
{
    uint32_t status = astra_port_receive_from(receive, message, capacity,
        handles, handle_capacity, size, count, sender);
    if (status == ASTRA_SYSCALL_RESOURCE_LIMIT && reclaim != NULL) {
        reclaim(context);
        status = astra_port_receive_from(receive, message, capacity,
            handles, handle_capacity, size, count, sender);
    }
    return status;
}

#endif
