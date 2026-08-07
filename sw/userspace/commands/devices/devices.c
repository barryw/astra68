/*
 * `devices` -- what the machine's interrupt endpoints are doing.
 *
 * **A quarantined device looks exactly like an idle one.** Its handles are
 * still open, its driver is still calling, and every call comes back with an
 * I/O error whose cause is three layers below the caller. There was no way to
 * ask the machine which of those it was: storage died partway through a
 * session, every read afterwards was ASTRA_BLOCK_IO_ERROR, and finding out why
 * meant adding trace points to the kernel and rebuilding it three times.
 *
 * This is that question, asked from the prompt. `state` says what the endpoint
 * is doing and `events` says why it stopped, and the sticky ones -- overflow,
 * storm, device-error -- are the ones that mean it will not start again on its
 * own.
 *
 * It reads ASTRA_SYSCALL_IRQ_ENDPOINT_INFO, which is every process's devices
 * at once and so takes the same diagnostic capability that reading the trace
 * stream does: the process handle, with DEBUG on it. A build that does not
 * carry that surface prints the refusal rather than an empty table, because a
 * table with nothing in it and a question nobody was allowed to ask are
 * different answers.
 */

#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/status.h>
#include <astra/stream.h>
#include <astra/syscall.h>

ASTRA_PROGRAM("devices", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static uint32_t out;

static void
say(const char *text)
{
    (void)astra_print(out, text);
}

static void
say_line(const char *text)
{
    (void)astra_print(out, text);
    (void)astra_print(out, "\n");
}

/* Right-aligned in `width`, because a column of numbers is read down. */
static void
say_number(uint32_t value, uint32_t width)
{
    char digits[12];
    char text[16];
    uint32_t count = 0u;
    uint32_t at = 0u;

    if (value == 0u) {
        digits[count++] = '0';
    }
    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count + at < width && at + 1u < sizeof(text)) {
        text[at++] = ' ';
    }
    while (count != 0u && at + 1u < sizeof(text)) {
        text[at++] = digits[--count];
    }
    text[at] = '\0';
    say(text);
}

static void
say_padded(const char *text, uint32_t width)
{
    uint32_t length = 0u;

    say(text);
    while (text[length] != '\0') {
        ++length;
    }
    while (length < width) {
        say(" ");
        ++length;
    }
}

static const char *
state_name(uint8_t state)
{
    switch (state) {
    case ASTRA_IRQ_ENDPOINT_FREE:
        return "free";
    case ASTRA_IRQ_ENDPOINT_MASKED:
        return "masked";
    case ASTRA_IRQ_ENDPOINT_ARMED:
        return "armed";
    case ASTRA_IRQ_ENDPOINT_PENDING:
        return "pending";
    case ASTRA_IRQ_ENDPOINT_REVOKING:
        return "revoking";
    default:
        return "?";
    }
}

/*
 * Why it stopped, in words. These are sticky: an endpoint carrying one answers
 * every read with it until something recovers the endpoint, so this is the
 * column that says a device is gone rather than quiet.
 */
static void
say_events(uint8_t flags)
{
    uint32_t written = 0u;

    if ((flags & ASTRA_IRQ_ENDPOINT_EVENT_DEVICE_ERROR) != 0u) {
        say("device-error ");
        ++written;
    }
    if ((flags & ASTRA_IRQ_ENDPOINT_EVENT_STORM) != 0u) {
        say("storm ");
        ++written;
    }
    if ((flags & ASTRA_IRQ_ENDPOINT_EVENT_OVERFLOW) != 0u) {
        say("overflow ");
        ++written;
    }
    if (written == 0u) {
        say("-");
    }
}

int
astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *capabilities;
    AstraIrqEndpointInfo info;
    uint32_t slots = 0u;
    uint32_t status;
    uint32_t shown = 0u;

    if (startup == NULL || startup->capabilities_address == 0u) {
        return ASTRA_STATUS_INVALID;
    }
    capabilities = (const AstraStartupCapability *)(uintptr_t)
        startup->capabilities_address;
    for (uint32_t index = 0u; index < startup->capability_count; ++index) {
        if (astra_capability_name_equal(capabilities[index].name, "STDOUT")) {
            out = capabilities[index].handle;
        }
    }
    if (out == 0u) {
        return ASTRA_STATUS_ACCESS;
    }

    /*
     * The first call is also the one that says how many slots there are, so a
     * refusal is reported before anything is printed rather than as a table
     * that stops early for a reason the reader has to guess.
     */
    status = astra_irq_endpoint_info(startup->process_handle, 0u, &info,
                                     &slots);
    if (status != ASTRA_SYSCALL_OK) {
        say("devices: refused, status ");
        say_number(status, 0u);
        say_line("");
        say_line("this build carries no diagnostic capability on its own "
                 "process");
        return (int)ASTRA_STATUS_ACCESS;
    }

    say_line("slot  src  owner       state     ipl  delivered  acked  "
             "pend  run  events");
    for (uint32_t slot = 0u; slot < slots; ++slot) {
        if (astra_irq_endpoint_info(startup->process_handle, slot, &info,
                                    NULL) != ASTRA_SYSCALL_OK) {
            break;
        }
        /* A free slot is a successful answer with nothing in it. */
        if (info.owner == 0u) {
            continue;
        }
        say_number(slot, 4u);
        say_number(info.source, 5u);
        say("  ");
        say_number(info.owner, 10u);
        say("  ");
        say_padded(state_name(info.state), 10u);
        say_number(info.ipl, 3u);
        say_number(info.delivered, 11u);
        say_number(info.acknowledged, 7u);
        say_number(info.pending_records, 6u);
        say_number(info.consecutive, 5u);
        say("  ");
        say_events(info.event_flags);
        say_line("");
        ++shown;
    }
    if (shown == 0u) {
        say_line("(no endpoints bound)");
    }
    return 0;
}
