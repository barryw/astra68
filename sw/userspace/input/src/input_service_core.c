#include <astra/input_service_core.h>
#include <astra/keymap.h>

#include <stddef.h>

#define HID_CAPS_LOCK 0x39u
#define HID_LEFT_CTRL 0xe0u
#define HID_RIGHT_GUI 0xe7u

static uint32_t logical_modifiers(const AstraInputService *service);

static AstraInputClient *find_client(AstraInputService *service,
                                     uint32_t client_id)
{
    for (uint32_t index = 0u; index < ASTRA_INPUT_CLIENT_MAX; ++index) {
        AstraInputClient *client = &service->clients[index];

        if (client->active != 0u && client->id == client_id)
            return client;
    }
    return NULL;
}

static void clear_keys(AstraInputService *service)
{
    for (uint32_t index = 0u; index < ASTRA_INPUT_HELD_WORDS; ++index)
        service->held[index] = 0u;
    service->modifiers = 0u;
    service->repeat_usage = 0u;
    service->repeat_deadline_ms = ASTRA_INPUT_REPEAT_DISABLED;
}

static AstraLogicalInputEvent make_event(AstraInputService *service,
                                         uint16_t type, uint16_t flags,
                                         uint32_t timestamp_ms)
{
    AstraLogicalInputEvent event = {
        .size = sizeof(AstraLogicalInputEvent),
        .version = ASTRA_INPUT_SERVICE_VERSION,
        .type = type,
        .flags = flags,
        .timestamp_ms = timestamp_ms,
        .sequence = ++service->logical_sequence,
        .focus_generation = service->focus_generation,
        .code = 0u,
        .modifiers = logical_modifiers(service),
        .value_x = 0,
        .value_y = 0,
    };

    if (event.sequence == 0u)
        event.sequence = ++service->logical_sequence;
    return event;
}

static AstraInputDeliveryResult raw_deliver(
    AstraInputService *service, AstraInputClient *client,
    const AstraLogicalInputEvent *event)
{
    AstraInputDeliveryResult result = client->deliver(client->context, event);

    if (result == ASTRA_INPUT_DELIVERY_OK) {
        ++service->stats.logical_events;
    } else if (result == ASTRA_INPUT_DELIVERY_FULL) {
        ++service->stats.queue_full;
    } else {
        ++service->stats.dead_clients;
        client->active = 0u;
        if (service->focus_id == client->id) {
            service->focus_id = 0u;
            ++service->focus_generation;
            clear_keys(service);
        }
    }
    return result;
}

static AstraInputDeliveryResult deliver(
    AstraInputService *service, AstraInputClient *client,
    const AstraLogicalInputEvent *event)
{
    if (client == NULL || client->active == 0u)
        return ASTRA_INPUT_DELIVERY_DEAD;
    if (client->desynchronized != 0u &&
        event->type != ASTRA_INPUT_EVENT_STATE_RESET) {
        AstraLogicalInputEvent reset = make_event(
            service, ASTRA_INPUT_EVENT_STATE_RESET,
            ASTRA_INPUT_LOGICAL_SYNTHETIC | ASTRA_INPUT_LOGICAL_LOSS,
            event->timestamp_ms);
        AstraInputDeliveryResult reset_result =
            raw_deliver(service, client, &reset);

        if (reset_result != ASTRA_INPUT_DELIVERY_OK)
            return reset_result;
        client->desynchronized = 0u;
    }
    return raw_deliver(service, client, event);
}

static uint32_t subscription_for(const AstraLogicalInputEvent *event);

/* A motion event carries the newest position, the motion totals and, in
   code, the pointer control sequence applied before it. */
static AstraLogicalInputEvent motion_event(AstraInputService *service,
                                           uint16_t flags,
                                           uint32_t timestamp_ms)
{
    AstraLogicalInputEvent motion = make_event(
        service, ASTRA_INPUT_EVENT_POINTER_MOTION, flags, timestamp_ms);

    motion.code = service->control_applied;
    motion.value_x = service->pointer_x;
    motion.value_y = service->pointer_y;
    motion.total_x = service->total_x;
    motion.total_y = service->total_y;
    return motion;
}

static void deliver_critical(AstraInputService *service,
                             AstraLogicalInputEvent *event)
{
    AstraInputClient *client = find_client(service, service->focus_id);

    if (client != NULL &&
        (client->subscriptions & subscription_for(event)) != 0u &&
        deliver(service, client, event) ==
                              ASTRA_INPUT_DELIVERY_FULL)
        client->desynchronized = 1u;
}

static bool pointer_button_is_wheel(uint32_t code)
{
    return code >= ASTRA_INPUT_BUTTON_WHEEL_UP &&
           code <= ASTRA_INPUT_BUTTON_WHEEL_RIGHT;
}

static uint32_t subscription_for(const AstraLogicalInputEvent *event)
{
    if (event->type == ASTRA_INPUT_EVENT_POINTER_MOTION)
        return ASTRA_INPUT_SUBSCRIBE_POINTER_MOTION;
    if (event->type == ASTRA_INPUT_EVENT_POINTER_BUTTON)
        return pointer_button_is_wheel(event->code) ?
            ASTRA_INPUT_SUBSCRIBE_POINTER_WHEEL :
            ASTRA_INPUT_SUBSCRIBE_POINTER_BUTTON;
    if (event->type == ASTRA_INPUT_EVENT_KEY)
        return ASTRA_INPUT_SUBSCRIBE_KEY;
    if (event->type == ASTRA_INPUT_EVENT_TEXT)
        return ASTRA_INPUT_SUBSCRIBE_TEXT;
    if (event->type == ASTRA_INPUT_EVENT_FOCUS)
        return ASTRA_INPUT_SUBSCRIBE_FOCUS;
    return 0u;
}

static void deliver_pointer(AstraInputService *service,
                            AstraLogicalInputEvent *event)
{
    uint32_t subscription = subscription_for(event);

    for (uint32_t index = 0u; index < ASTRA_INPUT_CLIENT_MAX; ++index) {
        AstraInputClient *client = &service->clients[index];

        if (client->active != 0u &&
            (client->subscriptions & subscription) != 0u &&
            deliver(service, client, event) == ASTRA_INPUT_DELIVERY_FULL) {
            /* Motion is the newest position and running totals: a client
               that is full takes the newest later, losing nothing. */
            if (event->type == ASTRA_INPUT_EVENT_POINTER_MOTION) {
                client->motion_pending = 1u;
                ++service->stats.coalesced_motion;
            } else {
                client->desynchronized = 1u;
            }
        } else if (client->active != 0u &&
                   event->type == ASTRA_INPUT_EVENT_POINTER_MOTION &&
                   (client->subscriptions & subscription) != 0u) {
            client->motion_pending = 0u;
        }
    }
}

static void repair_state(AstraInputService *service, uint32_t timestamp_ms,
                         bool generation_change)
{
    AstraLogicalInputEvent reset;

    clear_keys(service);
    reset = make_event(
        service, ASTRA_INPUT_EVENT_STATE_RESET,
        ASTRA_INPUT_LOGICAL_SYNTHETIC | ASTRA_INPUT_LOGICAL_LOSS,
        timestamp_ms);
    if (generation_change)
        ++service->stats.generation_repairs;
    else
        ++service->stats.overflow_repairs;
    for (uint32_t index = 0u; index < ASTRA_INPUT_CLIENT_MAX; ++index) {
        AstraInputClient *client = &service->clients[index];

        if (client->active != 0u &&
            deliver(service, client, &reset) == ASTRA_INPUT_DELIVERY_FULL)
            client->desynchronized = 1u;
    }
}

static uint32_t modifier_for_usage(uint32_t usage)
{
    if (usage < HID_LEFT_CTRL || usage > HID_RIGHT_GUI)
        return 0u;
    return UINT32_C(1) << (usage - HID_LEFT_CTRL);
}

static bool held(const AstraInputService *service, uint32_t usage)
{
    return (service->held[usage >> 5] &
            (UINT32_C(1) << (usage & 31u))) != 0u;
}

static void set_held(AstraInputService *service, uint32_t usage, bool down)
{
    uint32_t mask = UINT32_C(1) << (usage & 31u);

    if (down)
        service->held[usage >> 5] |= mask;
    else
        service->held[usage >> 5] &= ~mask;
}

static uint32_t keymap_modifiers(uint32_t modifiers)
{
    uint32_t mapped = 0u;

    if ((modifiers & ASTRA_INPUT_MOD_SHIFT) != 0u)
        mapped |= ASTRA_KEYMAP_MOD_SHIFT;
    if ((modifiers & ASTRA_INPUT_MOD_CTRL) != 0u)
        mapped |= ASTRA_KEYMAP_MOD_CONTROL;
    if ((modifiers & ASTRA_INPUT_MOD_CAPS_LOCK) != 0u)
        mapped |= ASTRA_KEYMAP_MOD_CAPS;
    return mapped;
}

static uint32_t logical_modifiers(const AstraInputService *service)
{
    uint32_t modifiers = service->modifiers;

    if ((modifiers & service->config.meta_modifier) != 0u)
        modifiers |= ASTRA_INPUT_MOD_META;
    return modifiers;
}

static uint32_t default_key_codepoint(uint32_t usage, uint32_t modifiers)
{
    uint32_t translated = astra_keymap_translate(
        usage, keymap_modifiers(modifiers));

    if (translated == ASTRA_KEYMAP_ENTER) return '\n';
    if (translated == ASTRA_KEYMAP_TAB) return '\t';
    return translated <= UINT8_MAX ? translated : 0u;
}

static void emit_key(AstraInputService *service, uint32_t usage, bool down,
                     bool repeat, uint32_t timestamp_ms)
{
    uint16_t flags = down ? ASTRA_INPUT_LOGICAL_DOWN : 0u;
    AstraLogicalInputEvent key;
    uint32_t codepoint;
    uint32_t modifiers = logical_modifiers(service);

    if (repeat)
        flags |= ASTRA_INPUT_LOGICAL_REPEAT;
    key = make_event(service, ASTRA_INPUT_EVENT_KEY, flags, timestamp_ms);
    key.code = usage;
    key.modifiers = modifiers;
    deliver_critical(service, &key);

    codepoint = service->config.translate != NULL ?
        service->config.translate(service->config.translate_context, usage,
                                  modifiers) :
        default_key_codepoint(usage, modifiers);
    if (down && codepoint != 0u &&
        (modifiers & (ASTRA_INPUT_MOD_CTRL | ASTRA_INPUT_MOD_ALT |
                      ASTRA_INPUT_MOD_GUI)) == 0u) {
        AstraLogicalInputEvent text = make_event(
            service, ASTRA_INPUT_EVENT_TEXT,
            repeat ? ASTRA_INPUT_LOGICAL_REPEAT : 0u, timestamp_ms);

        text.code = codepoint;
        text.modifiers = modifiers;
        deliver_critical(service, &text);
        ++service->stats.text_events;
    }
}

static int32_t accelerate(const AstraInputService *service, int32_t delta)
{
    uint32_t magnitude;
    uint64_t scaled;

    if (delta == 0)
        return 0;
    magnitude = delta < 0 ? (uint32_t)(-(int64_t)delta) : (uint32_t)delta;
    if (magnitude <= service->config.acceleration_threshold)
        return delta;
    scaled = service->config.acceleration_threshold +
        ((uint64_t)(magnitude - service->config.acceleration_threshold) *
         service->config.acceleration_numerator) /
            service->config.acceleration_denominator;
    if (scaled > INT32_MAX)
        scaled = INT32_MAX;
    return delta < 0 ? -(int32_t)scaled : (int32_t)scaled;
}

/* @p value kept in [low, high), the confinement on one axis; an empty
   range holds the pointer at @p current. */
static int32_t clamp_coordinate(int64_t value, int32_t low, int32_t high,
                                int32_t current)
{
    if (low >= high)
        return current;
    if (value < low)
        return low;
    if (value >= high)
        return high - 1;
    return (int32_t)value;
}

static int32_t clamp_x(const AstraInputService *service, int64_t value)
{
    return clamp_coordinate(value, service->confine_left,
                            service->confine_right, service->pointer_x);
}

static int32_t clamp_y(const AstraInputService *service, int64_t value)
{
    return clamp_coordinate(value, service->confine_top,
                            service->confine_bottom, service->pointer_y);
}

static void emit_motion(AstraInputService *service, uint32_t timestamp_ms)
{
    AstraLogicalInputEvent motion = motion_event(service, 0u, timestamp_ms);

    deliver_pointer(service, &motion);
}

static int32_t clip(int32_t value, int32_t low, int32_t high)
{
    return value < low ? low : value > high ? high : value;
}

bool astra_input_service_pointer_control(
    AstraInputService *service, const AstraInputPointerControl *control,
    uint32_t timestamp_ms)
{
    int32_t width;
    int32_t height;
    int32_t x;
    int32_t y;

    if (service == NULL || control == NULL)
        return false;
    if (control->sequence == service->control_applied)
        return true;
    width = (int32_t)service->config.pointer_width;
    height = (int32_t)service->config.pointer_height;
    service->confine_left = clip(control->left, 0, width);
    service->confine_right = clip(control->right, 0, width);
    service->confine_top = clip(control->top, 0, height);
    service->confine_bottom = clip(control->bottom, 0, height);
    x = service->pointer_x;
    y = service->pointer_y;
    if (control->warp != service->warp_applied) {
        service->warp_applied = control->warp;
        x = control->warp_x;
        y = control->warp_y;
    }
    service->pointer_x = clamp_x(service, x);
    service->pointer_y = clamp_y(service, y);
    service->control_applied = control->sequence;
    /* Every client sees where the pointer is now, flagged as not the
       device's doing; the totals do not move. */
    {
        AstraLogicalInputEvent motion = motion_event(
            service, ASTRA_INPUT_LOGICAL_SYNTHETIC, timestamp_ms);

        deliver_pointer(service, &motion);
    }
    return true;
}

bool astra_input_service_init(AstraInputService *service,
                              const AstraInputServiceConfig *config)
{
    if (service == NULL || config == NULL || config->pointer_width == 0u ||
        config->pointer_width > INT32_MAX || config->pointer_height == 0u ||
        config->pointer_height > INT32_MAX ||
        config->repeat_interval_ms == 0u ||
        config->acceleration_denominator == 0u ||
        config->acceleration_numerator == 0u ||
        (config->meta_modifier != 0u &&
         config->meta_modifier != ASTRA_INPUT_MOD_LEFT_ALT &&
         config->meta_modifier != ASTRA_INPUT_MOD_LEFT_GUI))
        return false;
    *service = (AstraInputService){0};
    service->config = *config;
    if (service->config.meta_modifier == 0u)
        service->config.meta_modifier = ASTRA_INPUT_MOD_LEFT_ALT;
    service->focus_generation = 1u;
    service->repeat_deadline_ms = ASTRA_INPUT_REPEAT_DISABLED;
    service->pointer_x = (int32_t)(config->pointer_width / 2u);
    service->pointer_y = (int32_t)(config->pointer_height / 2u);
    service->confine_right = (int32_t)config->pointer_width;
    service->confine_bottom = (int32_t)config->pointer_height;
    return true;
}

bool astra_input_service_attach(AstraInputService *service, uint32_t client_id,
                                AstraInputDelivery delivery, void *context)
{
    AstraInputClient *available = NULL;

    if (service == NULL || client_id == 0u || delivery == NULL ||
        find_client(service, client_id) != NULL)
        return false;
    for (uint32_t index = 0u; index < ASTRA_INPUT_CLIENT_MAX; ++index) {
        if (service->clients[index].active == 0u) {
            available = &service->clients[index];
            break;
        }
    }
    if (available == NULL)
        return false;
    ++available->generation;
    if (available->generation == 0u)
        ++available->generation;
    available->deliver = delivery;
    available->context = context;
    available->id = client_id;
    available->subscriptions = ASTRA_INPUT_SUBSCRIBE_ALL;
    available->active = 1u;
    available->desynchronized = 0u;
    available->motion_pending = 0u;
    available->reserved = 0u;
    return true;
}

bool astra_input_service_subscribe(AstraInputService *service,
                                   uint32_t client_id,
                                   uint32_t subscriptions,
                                   uint32_t timestamp_ms)
{
    AstraInputClient *client;

    if (service == NULL || subscriptions == 0u ||
        (subscriptions & ~ASTRA_INPUT_SUBSCRIBE_ALL) != 0u ||
        (client = find_client(service, client_id)) == NULL)
        return false;
    client->subscriptions = subscriptions;
    if ((subscriptions & ASTRA_INPUT_SUBSCRIBE_POINTER_MOTION) != 0u) {
        AstraLogicalInputEvent motion = motion_event(
            service, ASTRA_INPUT_LOGICAL_SYNTHETIC, timestamp_ms);

        if (deliver(service, client, &motion) == ASTRA_INPUT_DELIVERY_FULL) {
            client->motion_pending = 1u;
            ++service->stats.coalesced_motion;
        }
    }
    return true;
}

bool astra_input_service_detach(AstraInputService *service,
                                uint32_t client_id)
{
    AstraInputClient *client;

    if (service == NULL || (client = find_client(service, client_id)) == NULL)
        return false;
    if (service->focus_id == client_id) {
        service->focus_id = 0u;
        ++service->focus_generation;
        clear_keys(service);
    }
    client->active = 0u;
    client->deliver = NULL;
    client->context = NULL;
    return true;
}

bool astra_input_service_set_focus(AstraInputService *service,
                                   uint32_t client_id, uint32_t timestamp_ms)
{
    AstraInputClient *old_client;
    AstraInputClient *new_client;

    if (service == NULL || client_id == service->focus_id)
        return service != NULL;
    new_client = client_id == 0u ? NULL : find_client(service, client_id);
    if (client_id != 0u && new_client == NULL)
        return false;
    old_client = find_client(service, service->focus_id);
    if (old_client != NULL &&
        (old_client->subscriptions & ASTRA_INPUT_SUBSCRIBE_FOCUS) != 0u) {
        AstraLogicalInputEvent lost = make_event(
            service, ASTRA_INPUT_EVENT_FOCUS,
            ASTRA_INPUT_LOGICAL_SYNTHETIC, timestamp_ms);
        lost.code = old_client->id;
        if (deliver(service, old_client, &lost) == ASTRA_INPUT_DELIVERY_FULL)
            old_client->desynchronized = 1u;
    }
    clear_keys(service);
    service->focus_id = client_id;
    ++service->focus_generation;
    ++service->stats.focus_changes;
    if (new_client != NULL &&
        (new_client->subscriptions & ASTRA_INPUT_SUBSCRIBE_FOCUS) != 0u) {
        AstraLogicalInputEvent gained = make_event(
            service, ASTRA_INPUT_EVENT_FOCUS,
            ASTRA_INPUT_LOGICAL_SYNTHETIC | ASTRA_INPUT_LOGICAL_FOCUSED,
            timestamp_ms);
        gained.code = new_client->id;
        if (deliver(service, new_client, &gained) ==
            ASTRA_INPUT_DELIVERY_FULL)
            new_client->desynchronized = 1u;
    }
    return true;
}

static bool pointer_motion_event(const AstraInputEvent *event)
{
    uint32_t kind;

    if (event == NULL ||
        ASTRA_INPUT_EVENT_CLASS(event->header) != ASTRA_INPUT_CLASS_POINTER)
        return false;
    kind = ASTRA_INPUT_EVENT_KIND(event->header);
    return kind == ASTRA_INPUT_POINTER_RELATIVE ||
           kind == ASTRA_INPUT_POINTER_ABSOLUTE;
}

static void ingest_one(AstraInputService *service,
                       const AstraInputEvent *event,
                       bool physical_overflow, bool emit_pointer_motion)
{
    uint32_t event_class;
    uint32_t kind;
    uint32_t flags;

    if (service == NULL || event == NULL)
        return;
    ++service->stats.physical_events;
    if (service->physical_generation != 0u &&
        service->physical_generation != event->host_generation)
        repair_state(service, event->timestamp_ms, true);
    service->physical_generation = event->host_generation;
    if (physical_overflow)
        repair_state(service, event->timestamp_ms, false);
    event_class = ASTRA_INPUT_EVENT_CLASS(event->header);
    kind = ASTRA_INPUT_EVENT_KIND(event->header);
    flags = ASTRA_INPUT_EVENT_FLAGS(event->header);
    if (event_class == ASTRA_INPUT_CLASS_KEYBOARD &&
        kind == ASTRA_INPUT_KEY_PHYSICAL && event->value < 256u) {
        uint32_t usage = event->value;
        bool down = (flags & ASTRA_INPUT_FLAG_DOWN) != 0u;
        uint32_t modifier = modifier_for_usage(usage);

        if (down == held(service, usage))
            return;
        set_held(service, usage, down);
        if (modifier != 0u) {
            if (down)
                service->modifiers |= modifier;
            else
                service->modifiers &= ~modifier;
        } else if (usage == HID_CAPS_LOCK && down) {
            service->modifiers ^= ASTRA_INPUT_MOD_CAPS_LOCK;
        }
        emit_key(service, usage, down, false, event->timestamp_ms);
        if (down && modifier == 0u && usage != HID_CAPS_LOCK) {
            service->repeat_usage = (uint16_t)usage;
            service->repeat_deadline_ms = event->timestamp_ms +
                                          service->config.repeat_delay_ms;
        } else if (!down && service->repeat_usage == usage) {
            service->repeat_usage = 0u;
            service->repeat_deadline_ms = ASTRA_INPUT_REPEAT_DISABLED;
        }
        return;
    }
    if (event_class != ASTRA_INPUT_CLASS_POINTER)
        return;
    if (kind == ASTRA_INPUT_POINTER_RELATIVE) {
        int32_t delta = accelerate(service, (int32_t)event->value);

        /* The totals take the device's motion as it came: wrapping, not
           clamped, not accelerated. */
        if ((flags & ASTRA_INPUT_FLAG_AXIS_Y) != 0u) {
            service->total_y = (int32_t)((uint32_t)service->total_y +
                                         event->value);
            service->pointer_y =
                clamp_y(service, (int64_t)service->pointer_y + delta);
        } else {
            service->total_x = (int32_t)((uint32_t)service->total_x +
                                         event->value);
            service->pointer_x =
                clamp_x(service, (int64_t)service->pointer_x + delta);
        }
        if (emit_pointer_motion)
            emit_motion(service, event->timestamp_ms);
    } else if (kind == ASTRA_INPUT_POINTER_ABSOLUTE) {
        /* An absolute device's motion is how far it moved. */
        if ((flags & ASTRA_INPUT_FLAG_AXIS_Y) != 0u) {
            if ((service->absolute_seen & 2u) != 0u)
                service->total_y = (int32_t)(
                    (uint32_t)service->total_y +
                    ((uint32_t)event->value - (uint32_t)service->absolute_y));
            service->absolute_y = (int32_t)event->value;
            service->absolute_seen |= 2u;
            service->pointer_y = clamp_y(service, (int32_t)event->value);
        } else {
            if ((service->absolute_seen & 1u) != 0u)
                service->total_x = (int32_t)(
                    (uint32_t)service->total_x +
                    ((uint32_t)event->value - (uint32_t)service->absolute_x));
            service->absolute_x = (int32_t)event->value;
            service->absolute_seen |= 1u;
            service->pointer_x = clamp_x(service, (int32_t)event->value);
        }
        if (emit_pointer_motion)
            emit_motion(service, event->timestamp_ms);
    } else if (kind == ASTRA_INPUT_POINTER_BUTTON) {
        AstraLogicalInputEvent button = make_event(
            service, ASTRA_INPUT_EVENT_POINTER_BUTTON,
            (flags & ASTRA_INPUT_FLAG_DOWN) != 0u ?
                ASTRA_INPUT_LOGICAL_DOWN : 0u,
            event->timestamp_ms);

        button.code = event->value;
        button.value_x = service->pointer_x;
        button.value_y = service->pointer_y;
        deliver_pointer(service, &button);
    }
}

void astra_input_service_ingest(AstraInputService *service,
                                const AstraInputEvent *event,
                                bool physical_overflow)
{
    ingest_one(service, event, physical_overflow, true);
}

void astra_input_service_ingest_batch(AstraInputService *service,
                                      const AstraInputEvent *events,
                                      uint32_t count,
                                      bool physical_overflow)
{
    uint32_t motion_timestamp = 0u;
    bool motion_pending = false;

    if (service == NULL || events == NULL)
        return;
    for (uint32_t index = 0u; index < count; ++index) {
        bool motion = pointer_motion_event(&events[index]);

        if (!motion && motion_pending) {
            emit_motion(service, motion_timestamp);
            motion_pending = false;
        }
        ingest_one(service, &events[index], physical_overflow && index == 0u,
                   !motion);
        if (motion) {
            motion_timestamp = events[index].timestamp_ms;
            motion_pending = true;
        }
    }
    if (motion_pending)
        emit_motion(service, motion_timestamp);
}

void astra_input_service_tick(AstraInputService *service,
                              uint32_t timestamp_ms)
{
    AstraInputClient *client;

    if (service == NULL)
        return;
    for (uint32_t index = 0u; index < ASTRA_INPUT_CLIENT_MAX; ++index) {
        AstraInputClient *desynchronized = &service->clients[index];

        if (desynchronized->active != 0u &&
            desynchronized->desynchronized != 0u) {
            AstraLogicalInputEvent reset = make_event(
                service, ASTRA_INPUT_EVENT_STATE_RESET,
                ASTRA_INPUT_LOGICAL_SYNTHETIC | ASTRA_INPUT_LOGICAL_LOSS,
                timestamp_ms);

            if (raw_deliver(service, desynchronized, &reset) ==
                ASTRA_INPUT_DELIVERY_OK)
                desynchronized->desynchronized = 0u;
        }
    }
    for (uint32_t index = 0u; index < ASTRA_INPUT_CLIENT_MAX; ++index) {
        client = &service->clients[index];
        if (client->active != 0u && client->motion_pending != 0u) {
            AstraLogicalInputEvent motion =
                motion_event(service, 0u, timestamp_ms);

            if (deliver(service, client, &motion) == ASTRA_INPUT_DELIVERY_OK)
                client->motion_pending = 0u;
        }
    }
    if (service->repeat_usage != 0u &&
        service->repeat_deadline_ms != ASTRA_INPUT_REPEAT_DISABLED &&
        (int32_t)(timestamp_ms - service->repeat_deadline_ms) >= 0) {
        emit_key(service, service->repeat_usage, true, true, timestamp_ms);
        service->repeat_deadline_ms = timestamp_ms +
                                      service->config.repeat_interval_ms;
        ++service->stats.repeat_events;
    }
}

uint32_t astra_input_service_next_delay(const AstraInputService *service,
                                        uint32_t timestamp_ms)
{
    int32_t remaining;

    if (service == NULL || service->repeat_usage == 0u ||
        service->repeat_deadline_ms == ASTRA_INPUT_REPEAT_DISABLED)
        return ASTRA_INPUT_REPEAT_DISABLED;
    remaining = (int32_t)(service->repeat_deadline_ms - timestamp_ms);
    return remaining > 0 ? (uint32_t)remaining : 0u;
}

bool astra_input_service_stats(const AstraInputService *service,
                               AstraInputServiceStats *stats)
{
    if (service == NULL || stats == NULL)
        return false;
    *stats = service->stats;
    return true;
}
