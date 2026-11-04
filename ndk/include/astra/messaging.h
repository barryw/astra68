#ifndef ASTRA_MESSAGING_H
#define ASTRA_MESSAGING_H

/** @file messaging.h @brief Process directory and bounded event routing. */

#include <astra/attributes.h>
#include <astra/messaging_abi.h>
#include <astra/resource.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

/** Identify caller from kernel-authenticated process ID and launch metadata.
 * @param broker MESSAGING startup capability.
 * @param identity Receives kind, name, and process ID.
 * @return Astra result.
 */
ASTRA_NODISCARD AstraResult astra_messaging_identify(
    AstraHandle broker, AstraMessagingReply *identity);
/** Enumerate next live service, application or registered command by PID.
 * @param broker MESSAGING startup capability.
 * @param cursor Previous entry's cursor, or zero to start.
 * @param entry Receives next identity and endpoint availability.
 * @return Astra result; NOT_PRESENT ends enumeration.
 */
ASTRA_NODISCARD AstraResult astra_messaging_list(
    AstraHandle broker, uint64_t cursor, AstraMessagingReply *entry);
/** Find direct endpoint by kind, name and optional exact process ID.
 * @param broker MESSAGING startup capability.
 * @param kind Service, application, or command.
 * @param name Exact name, or empty for a command selected by PID.
 * @param process_id Exact PID, or zero for first matching process.
 * @param entry Receives matched identity.
 * @param endpoint Receives owned send/wait capability for peer port calls.
 * @return Astra result.
 */
ASTRA_NODISCARD AstraResult astra_messaging_lookup(
    AstraHandle broker, uint32_t kind, const char *name, uint32_t process_id,
    AstraMessagingReply *entry, AstraHandle *endpoint);
/** Transfer one direct send endpoint for caller's process.
 * @param broker MESSAGING startup capability.
 * @param endpoint Owned send endpoint; consumed on successful transfer.
 * @return Astra result.
 */
ASTRA_NODISCARD AstraResult astra_messaging_register(
    AstraHandle broker, AstraHandle *endpoint);
/** Remove caller's direct endpoint.
 * @param broker MESSAGING startup capability.
 * @return Astra result.
 */
ASTRA_NODISCARD AstraResult astra_messaging_unregister(AstraHandle broker);

/** Subscribe to publisher identity and topic; transfer event send endpoint.
 * @param broker MESSAGING startup capability.
 * @param kind Publisher kind.
 * @param name Publisher name; empty selects a command by PID.
 * @param process_id Exact publisher PID, or zero for all named instances.
 * @param topic Publisher-local topic.
 * @param event_send Owned send endpoint; consumed on successful transfer.
 * @param subscription Receives owner-bound subscription ID.
 * @return Astra result.
 */
ASTRA_NODISCARD AstraResult astra_messaging_subscribe(
    AstraHandle broker, uint32_t kind, const char *name, uint32_t process_id,
    const char *topic, AstraHandle *event_send, uint64_t *subscription);
/** Remove caller-owned subscription; process exit also removes it.
 * @param broker MESSAGING startup capability.
 * @param subscription Owner-bound subscription ID.
 * @return Astra result.
 */
ASTRA_NODISCARD AstraResult astra_messaging_unsubscribe(
    AstraHandle broker, uint64_t subscription);

/** Fan out an event, optionally copying response send to each recipient.
 * @param broker MESSAGING startup capability.
 * @param topic Publisher-local topic.
 * @param payload Opaque event bytes.
 * @param payload_size Byte count, at most ASTRA_MESSAGING_PAYLOAD_MAX.
 * @param transaction_id Nonzero caller transaction ID.
 * @param response_send Optional owned response send; consumed on transfer.
 * @param delivered Receives successful recipient count.
 * @param dropped Receives full or dead recipient count.
 * @return Astra result.
 */
ASTRA_NODISCARD AstraResult astra_messaging_publish(
    AstraHandle broker, const char *topic, const void *payload,
    uint32_t payload_size, uint32_t transaction_id,
    AstraHandle *response_send, uint32_t *delivered, uint32_t *dropped);
/** Post without waiting; retry WOULD_BLOCK when state change matters.
 * @param broker MESSAGING startup capability.
 * @param topic Publisher-local topic.
 * @param payload Opaque event bytes.
 * @param payload_size Byte count, at most ASTRA_MESSAGING_PAYLOAD_MAX.
 * @param transaction_id Nonzero caller transaction ID.
 * @return Astra result; acceptance does not guarantee recipient delivery.
 */
ASTRA_NODISCARD AstraResult astra_messaging_post(
    AstraHandle broker, const char *topic, const void *payload,
    uint32_t payload_size, uint32_t transaction_id);
/** Try one event receive; caller owns optional transferred reply send.
 * @param event_receive Subscriber's receive endpoint.
 * @param event Receives validated broker event.
 * @param response_send Optional output for owned reply send capability.
 * @return Astra result; WOULD_BLOCK means queue empty.
 */
ASTRA_NODISCARD AstraResult astra_messaging_event_try(
    AstraHandle event_receive, AstraMessagingEvent *event,
    AstraHandle *response_send);

ASTRA_EXTERN_C_END
#endif
