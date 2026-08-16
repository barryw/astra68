#ifndef ASTRA_FRONT_PANEL_H
#define ASTRA_FRONT_PANEL_H

/**
 * @file front_panel.h
 * @brief Managed access to the board buttons, DIP switches, and LEDs.
 */

#include <stdint.h>

#include <astra/resource.h>

ASTRA_EXTERN_C_BEGIN

/**
 * @defgroup astra_front_panel Front panel
 * @brief Input sampling, change detection, and leased LED output.
 *
 * The six user buttons and four DIP switches are shared read-only inputs. The
 * eight LEDs are independently leasable output bits, allowing multiple
 * applications to own disjoint LED sets without overwriting each other.
 *
 * Live ::AstraFrontPanelLedLease values are move-only by convention: do not
 * copy them, and release each successful acquisition exactly once.
 *
 * @{
 */

/** Debounced button bits returned in ::AstraFrontPanelState. */
enum {
    /** Primary fire button. */
    ASTRA_PANEL_BUTTON_FIRE1 = 1u << 0,
    /** Secondary fire button. */
    ASTRA_PANEL_BUTTON_FIRE2 = 1u << 1,
    /** Up direction button. */
    ASTRA_PANEL_BUTTON_UP = 1u << 2,
    /** Down direction button. */
    ASTRA_PANEL_BUTTON_DOWN = 1u << 3,
    /** Left direction button. */
    ASTRA_PANEL_BUTTON_LEFT = 1u << 4,
    /** Right direction button. */
    ASTRA_PANEL_BUTTON_RIGHT = 1u << 5,
    /** Mask selecting every user button. */
    ASTRA_PANEL_BUTTON_ALL = 0x3fu
};

/** DIP-switch bits returned in ::AstraFrontPanelState. */
enum {
    /** DIP switch 1. */
    ASTRA_PANEL_SWITCH_1 = 1u << 0,
    /** DIP switch 2. */
    ASTRA_PANEL_SWITCH_2 = 1u << 1,
    /** DIP switch 3. */
    ASTRA_PANEL_SWITCH_3 = 1u << 2,
    /** DIP switch 4. */
    ASTRA_PANEL_SWITCH_4 = 1u << 3,
    /** Mask selecting every DIP switch. */
    ASTRA_PANEL_SWITCH_ALL = 0x0fu
};

/** Capability bits reported by ::AstraFrontPanelInfo. */
enum {
    /** Raw, un-debounced input samples are available. */
    ASTRA_PANEL_FEATURE_RAW_INPUT = 1u << 0,
    /** Input transitions are latched until explicitly cleared. */
    ASTRA_PANEL_FEATURE_CHANGE_LATCH = 1u << 1,
    /** Hardware enforces LED ownership masks. */
    ASTRA_PANEL_FEATURE_LED_OWNERSHIP = 1u << 2,
    /** Atomic LED set, clear, and toggle operations are available. */
    ASTRA_PANEL_FEATURE_ATOMIC_LEDS = 1u << 3,
    /** Three consecutive LED bits drive one RGB lamp's red, green, and blue channels. */
    ASTRA_PANEL_FEATURE_RGB_LED = 1u << 4
};

/** Change-latch and LED masks. */
enum {
    /** Button bits in the 16-bit change latch. */
    ASTRA_PANEL_CHANGE_BUTTON_MASK = 0x003fu,
    /** DIP-switch bits in the 16-bit change latch. */
    ASTRA_PANEL_CHANGE_SWITCH_MASK = 0x0f00u,
    /** Every defined input change-latch bit. */
    ASTRA_PANEL_CHANGE_ALL = 0x0f3fu,
    /** Every front-panel LED bit. */
    ASTRA_PANEL_LED_ALL = 0xffu
};

/**
 * Static front-panel capabilities.
 *
 * @since 0.1.0
 */
typedef struct AstraFrontPanelInfo {
    /** Number of controllable LEDs. */
    uint8_t led_count;
    /** Number of user buttons, excluding reset. */
    uint8_t button_count;
    /** Number of DIP switches. */
    uint8_t switch_count;
    /** Bitwise combination of `ASTRA_PANEL_FEATURE_*` values. */
    uint8_t features;
    /** Hardware ABI version encoded as `major << 16 | minor`. */
    uint32_t hardware_version;
    /** Reserved for source-compatible growth; currently zero. */
    uint32_t reserved[3];
} AstraFrontPanelInfo;

/**
 * One sampled front-panel input state.
 *
 * @since 0.1.0
 */
typedef struct AstraFrontPanelState {
    /** Debounced `ASTRA_PANEL_BUTTON_*` bits. */
    uint8_t buttons;
    /** Debounced `ASTRA_PANEL_SWITCH_*` bits. */
    uint8_t switches;
    /** Raw, un-debounced `ASTRA_PANEL_BUTTON_*` bits. */
    uint8_t raw_buttons;
    /** Raw, un-debounced `ASTRA_PANEL_SWITCH_*` bits. */
    uint8_t raw_switches;
} AstraFrontPanelState;

/**
 * Exclusive ownership of one or more front-panel LEDs.
 *
 * Initialize with ::ASTRA_FRONT_PANEL_LED_LEASE_INIT or
 * ::ASTRA_AUTO_FRONT_PANEL_LED_LEASE. The private token must not be inspected
 * or modified. A live lease is move-only by convention and must not be copied.
 *
 * @since 0.1.0
 */
typedef struct AstraFrontPanelLedLease {
    /** Private NDK handle; applications must not access this field. */
    AstraHandle _private_handle;
} AstraFrontPanelLedLease;

/** Initializer for an empty ::AstraFrontPanelLedLease. */
#define ASTRA_FRONT_PANEL_LED_LEASE_INIT { ASTRA_INVALID_HANDLE }
/**
 * Declare an empty LED lease that releases itself on normal scope exit.
 *
 * This extension is available with GCC and Clang. Cleanup failures are ignored;
 * use astra_front_panel_release_leds() explicitly when the result matters.
 */
#define ASTRA_AUTO_FRONT_PANEL_LED_LEASE(name) \
    AstraFrontPanelLedLease name \
        ASTRA_CLEANUP(astra_front_panel_led_lease_cleanup) = \
        ASTRA_FRONT_PANEL_LED_LEASE_INIT

/**
 * Test whether a compatible front-panel device is present.
 *
 * @return Nonzero when a supported device is available, otherwise zero.
 *
 * @par Blocking
 * Never blocks.
 * @par Thread safety
 * Reentrant.
 * @since 0.1.0
 */
int astra_front_panel_present(void);

/**
 * Read static front-panel capabilities.
 *
 * @param[out] info Receives the capabilities on success.
 * @retval ASTRA_OK Capabilities were returned.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT @p info is null.
 * @retval ASTRA_ERROR_NOT_PRESENT No compatible front panel is available.
 *
 * @par Ownership
 * No ownership is transferred.
 * @par Blocking
 * Never blocks.
 * @par Thread safety
 * Reentrant.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_front_panel_get_info(AstraFrontPanelInfo *info);

/**
 * Sample debounced and raw button and switch inputs.
 *
 * @param[out] state Receives one coherent input sample on success.
 * @retval ASTRA_OK The input state was returned.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT @p state is null.
 *
 * @pre astra_front_panel_present() returned nonzero.
 * @par Ownership
 * No ownership is transferred.
 * @par Blocking
 * Never blocks.
 * @par Thread safety
 * Reentrant.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_front_panel_read(AstraFrontPanelState *state);

/**
 * Read input bits that have changed since they were last cleared.
 *
 * @param[out] changes Receives `ASTRA_PANEL_CHANGE_*` bits.
 * @retval ASTRA_OK The change latch was returned.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT @p changes is null.
 *
 * @pre astra_front_panel_present() returned nonzero.
 * @par Blocking
 * Never blocks.
 * @par Thread safety
 * Reentrant.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_front_panel_get_changes(uint16_t *changes);

/**
 * Clear selected input bits from the change latch.
 *
 * Undefined bits in @p changes are ignored. A transition concurrent with the
 * clear remains latched by the hardware.
 *
 * @param[in] changes `ASTRA_PANEL_CHANGE_*` bits to acknowledge.
 * @retval ASTRA_OK The selected bits were acknowledged.
 *
 * @pre astra_front_panel_present() returned nonzero.
 * @par Blocking
 * Never blocks.
 * @par Thread safety
 * Safe for concurrent calls; the hardware operation is write-one-to-clear.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_front_panel_clear_changes(uint16_t changes);

/**
 * Read the currently displayed LED value without acquiring ownership.
 *
 * @param[out] value Receives all eight LED bits.
 * @retval ASTRA_OK The LED state was returned.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT @p value is null.
 *
 * @pre astra_front_panel_present() returned nonzero.
 * @par Ownership
 * No ownership is transferred; this call does not grant write access.
 * @par Blocking
 * Never blocks.
 * @par Thread safety
 * Reentrant.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_front_panel_get_leds(uint8_t *value);

/**
 * Acquire exclusive ownership of selected LED bits.
 *
 * @param[in] mask Nonzero combination of LED bits to acquire.
 * @param[in] options Acquisition policy, or null for the default exclusive,
 * immediate policy. The 0.1 direct-MMIO backend does not support shared leases.
 * @param[in,out] lease Empty initialized lease that receives ownership.
 * @retval ASTRA_OK Ownership was acquired and stored in @p lease.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT An argument or lease state is invalid.
 * @retval ASTRA_ERROR_UNSUPPORTED Shared ownership was requested.
 * @retval ASTRA_ERROR_NOT_PRESENT No compatible front panel is available.
 * @retval ASTRA_ERROR_BUSY At least one requested LED is already owned.
 * @retval ASTRA_ERROR_NO_RESOURCES No local lease slot is available.
 *
 * @par Ownership
 * On success, @p lease owns every bit in @p mask. On failure it remains empty.
 * The live lease must not be copied.
 * @par Blocking
 * The 0.1 direct-MMIO backend never blocks and reports contention as
 * ::ASTRA_ERROR_BUSY. An OS backend may honor the supplied wait policy.
 * @par Thread safety
 * Serialize concurrent acquire and release operations when using the current
 * direct-MMIO backend. The OS backend serializes its process handle table.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_front_panel_acquire_leds(
    uint8_t mask,
    const AstraAcquireOptions *options,
    AstraFrontPanelLedLease *lease);

/**
 * Release an LED lease and invalidate it.
 *
 * @param[in,out] lease Live lease to consume.
 * @retval ASTRA_OK Ownership was released and @p lease is now empty.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT @p lease is null.
 * @retval ASTRA_ERROR_INVALID_HANDLE The lease is empty, stale, or invalid.
 *
 * @par Ownership
 * Consumes the live lease on success. A failed release leaves it unchanged.
 * @par Blocking
 * Never blocks in the direct-MMIO backend.
 * @par Thread safety
 * Do not use one lease concurrently. Serialize acquire and release operations
 * when using the current direct-MMIO backend.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_front_panel_release_leds(
    AstraFrontPanelLedLease *lease);

/**
 * Replace every LED bit owned by a lease.
 *
 * Bits outside the lease are preserved. Owned bits are copied from @p value.
 *
 * @param[in] lease Live LED lease.
 * @param[in] value Desired value for the owned bits.
 * @retval ASTRA_OK Owned LED bits were updated.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT @p lease is null.
 * @retval ASTRA_ERROR_INVALID_HANDLE The lease is empty, stale, or invalid.
 *
 * @par Ownership
 * The lease remains live and owned by the caller.
 * @par Blocking
 * Never blocks.
 * @par Thread safety
 * Do not operate on one lease concurrently from multiple execution contexts.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_front_panel_set_leds(
    const AstraFrontPanelLedLease *lease, uint8_t value);

/**
 * Atomically set selected LED bits owned by a lease.
 *
 * Bits selected by @p mask but not owned by @p lease are ignored.
 *
 * @param[in] lease Live LED lease.
 * @param[in] mask LED bits to set.
 * @retval ASTRA_OK The owned selected bits were set.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT @p lease is null.
 * @retval ASTRA_ERROR_INVALID_HANDLE The lease is empty, stale, or invalid.
 *
 * @par Ownership
 * The lease remains live and owned by the caller.
 * @par Blocking
 * Never blocks.
 * @par Thread safety
 * Do not operate on one lease concurrently from multiple execution contexts.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_front_panel_set_led_bits(
    const AstraFrontPanelLedLease *lease, uint8_t mask);

/**
 * Atomically clear selected LED bits owned by a lease.
 *
 * Bits selected by @p mask but not owned by @p lease are ignored.
 *
 * @param[in] lease Live LED lease.
 * @param[in] mask LED bits to clear.
 * @retval ASTRA_OK The owned selected bits were cleared.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT @p lease is null.
 * @retval ASTRA_ERROR_INVALID_HANDLE The lease is empty, stale, or invalid.
 *
 * @par Ownership
 * The lease remains live and owned by the caller.
 * @par Blocking
 * Never blocks.
 * @par Thread safety
 * Do not operate on one lease concurrently from multiple execution contexts.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_front_panel_clear_led_bits(
    const AstraFrontPanelLedLease *lease, uint8_t mask);

/**
 * Atomically invert selected LED bits owned by a lease.
 *
 * Bits selected by @p mask but not owned by @p lease are ignored.
 *
 * @param[in] lease Live LED lease.
 * @param[in] mask LED bits to toggle.
 * @retval ASTRA_OK The owned selected bits were toggled.
 * @retval ASTRA_ERROR_INVALID_ARGUMENT @p lease is null.
 * @retval ASTRA_ERROR_INVALID_HANDLE The lease is empty, stale, or invalid.
 *
 * @par Ownership
 * The lease remains live and owned by the caller.
 * @par Blocking
 * Never blocks.
 * @par Thread safety
 * Do not operate on one lease concurrently from multiple execution contexts.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_front_panel_toggle_led_bits(
    const AstraFrontPanelLedLease *lease, uint8_t mask);

/**
 * Cleanup callback used by ::ASTRA_AUTO_FRONT_PANEL_LED_LEASE.
 *
 * An empty or null lease is ignored. A live lease is released, and any release
 * error is intentionally discarded because cleanup callbacks cannot report it.
 * Applications normally call the declaration macro rather than this function.
 *
 * @param[in,out] lease Lease to release when live.
 *
 * @par Blocking
 * Never blocks in the direct-MMIO backend.
 * @par Thread safety
 * Do not invoke concurrently with another operation on the same lease.
 * @since 0.1.0
 */
void astra_front_panel_led_lease_cleanup(AstraFrontPanelLedLease *lease);

/** @} */

ASTRA_EXTERN_C_END

#endif
