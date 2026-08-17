#include <astra/messaging_library.h>

#include <assert.h>

extern const AstraMessagingLibraryV1 astra_library_exports;

#define STUB(result, name, arguments) \
    AstraResult name arguments { return (result); }

STUB(ASTRA_OK, astra_handle_close, (AstraHandle *handle))
STUB(ASTRA_OK, astra_handle_duplicate,
     (AstraHandle source, uint32_t rights, AstraHandle *duplicate))
STUB(ASTRA_OK, astra_message_header_init,
     (AstraMessageHeader *header, uint32_t size, uint32_t protocol,
      uint16_t version, uint32_t operation, uint32_t transaction))
STUB(ASTRA_OK, astra_port_create,
     (uint32_t messages, uint32_t bytes, AstraPort *port))
STUB(ASTRA_OK, astra_port_close, (AstraPort *port))
STUB(ASTRA_OK, astra_port_send_try,
     (AstraHandle endpoint, const void *message, uint32_t size,
      AstraHandle *handles, uint32_t handle_count))
STUB(ASTRA_OK, astra_port_send_until,
     (AstraHandle endpoint, const void *message, uint32_t size,
      AstraHandle *handles, uint32_t handle_count,
      AstraMonotonicDeadline deadline))
STUB(ASTRA_OK, astra_port_receive_try,
     (AstraHandle endpoint, void *message, uint32_t capacity,
      AstraHandle *handles, uint32_t handle_capacity, uint32_t *size,
      uint32_t *handle_count))
STUB(ASTRA_OK, astra_port_receive_until,
     (AstraHandle endpoint, void *message, uint32_t capacity,
      AstraHandle *handles, uint32_t handle_capacity, uint32_t *size,
      uint32_t *handle_count, AstraMonotonicDeadline deadline))

int main(void)
{
    const AstraMessagingLibraryV1 *library = &astra_library_exports;

    assert(library->abi_major == ASTRA_MESSAGING_LIBRARY_ABI_MAJOR);
    assert(library->abi_minor == ASTRA_MESSAGING_LIBRARY_ABI_MINOR);
    assert(library->structure_size == sizeof(*library));
    assert(library->handle_close == astra_handle_close);
    assert(library->handle_duplicate == astra_handle_duplicate);
    assert(library->message_header_init == astra_message_header_init);
    assert(library->port_create == astra_port_create);
    assert(library->port_close == astra_port_close);
    assert(library->port_send_try == astra_port_send_try);
    assert(library->port_send_until == astra_port_send_until);
    assert(library->port_receive_try == astra_port_receive_try);
    assert(library->port_receive_until == astra_port_receive_until);
    return 0;
}
