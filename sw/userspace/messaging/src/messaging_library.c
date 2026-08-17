#include <astra/messaging_library.h>

#include <astra/library.h>

ASTRA_LIBRARY("messaging.library", 1, 0, 0,
              ASTRA_MESSAGING_LIBRARY_ABI_MAJOR,
              ASTRA_MESSAGING_LIBRARY_ABI_MINOR,
              "Barry Walker", "Copyright 2026 Barry Walker");

const AstraMessagingLibraryV1 astra_library_exports ASTRA_LIBRARY_EXPORTS = {
    ASTRA_MESSAGING_LIBRARY_ABI_MAJOR,
    ASTRA_MESSAGING_LIBRARY_ABI_MINOR,
    sizeof(AstraMessagingLibraryV1),
    astra_handle_close,
    astra_handle_duplicate,
    astra_message_header_init,
    astra_port_create,
    astra_port_close,
    astra_port_send_try,
    astra_port_send_until,
    astra_port_receive_try,
    astra_port_receive_until,
};
