// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L

#include <astra/display_capture.h>

#include "astra_display_capture_uapi.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <rfb/keysym.h>
#include <rfb/rfb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define ASTRA_CAPTURE_DEVICE "/dev/astra-display-capture"
#define ASTRA_QMP_DEFAULT "/run/astra/remote-desktop-qmp.sock"
#define ASTRA_CONTROL_DEFAULT "/run/astra/remote-desktop-control.sock"
#define ASTRA_RFB_PORT 5900
#define ASTRA_RFB_IDLE_US 100000L
#define ASTRA_RFB_PASSWORD_MAX 8u
#define ASTRA_INPUT_STATUS_ADDRESS UINT32_C(0xfff0070c)
#define ASTRA_INPUT_LEVEL_MASK UINT32_C(0x1f)

struct astra_remote_key {
    const char *qcode;
    unsigned int references;
    bool sent_down;
    struct astra_remote_key *next;
};

struct astra_remote_client_key {
    rfbKeySym symbol;
    struct astra_remote_key *key;
    struct astra_remote_key *modifier;
    struct astra_remote_client_key *next;
};

struct astra_remote_client {
    struct astra_remote_client_key *keys;
    unsigned int buttons;
};

struct astra_remote_desktop {
    const char *qmp_path;
    FILE *qmp;
    struct astra_remote_key *keys;
    unsigned int button_references[3];
    unsigned int sent_buttons;
    int pointer_x;
    int pointer_y;
    bool pointer_dirty;
};

struct astra_remote_server {
    struct astra_remote_desktop desktop;
    rfbScreenInfoPtr screen;
    const uint8_t *frame;
    uint8_t *previous_frame;
    char *passwords[2];
    int capture;
    bool have_frame;
    unsigned int generation;
};

struct astra_remote_damage {
    unsigned int x1;
    unsigned int y1;
    unsigned int x2;
    unsigned int y2;
};

static volatile sig_atomic_t astra_remote_running = 1;

static void astra_remote_stop(int signal_number)
{
    (void)signal_number;
    astra_remote_running = 0;
}

static const char *astra_qcode_for_keysym(rfbKeySym symbol)
{
    static const char letters[][2] = {
        "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l",
        "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x",
        "y", "z",
    };
    static const char digits[][2] = {
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    };
    static const char *const function_keys[] = {
        "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10",
        "f11", "f12", "f13", "f14", "f15", "f16", "f17", "f18", "f19",
        "f20", "f21", "f22", "f23", "f24",
    };

    if (symbol >= XK_A && symbol <= XK_Z)
        return letters[symbol - XK_A];
    if (symbol >= XK_a && symbol <= XK_z)
        return letters[symbol - XK_a];
    if (symbol >= XK_0 && symbol <= XK_9)
        return digits[symbol - XK_0];
    if (symbol >= XK_F1 && symbol <= XK_F24)
        return function_keys[symbol - XK_F1];
    switch (symbol) {
    case XK_space: return "spc";
    case XK_exclam: return "1";
    case XK_quotedbl: return "apostrophe";
    case XK_numbersign: return "3";
    case XK_dollar: return "4";
    case XK_percent: return "5";
    case XK_ampersand: return "7";
    case XK_apostrophe: return "apostrophe";
    case XK_parenleft: return "9";
    case XK_parenright: return "0";
    case XK_asterisk: return "8";
    case XK_plus: return "equal";
    case XK_comma: return "comma";
    case XK_minus: return "minus";
    case XK_period: return "dot";
    case XK_slash: return "slash";
    case XK_colon: return "semicolon";
    case XK_semicolon: return "semicolon";
    case XK_less: return "comma";
    case XK_equal: return "equal";
    case XK_greater: return "dot";
    case XK_question: return "slash";
    case XK_at: return "2";
    case XK_bracketleft: return "bracket_left";
    case XK_backslash: return "backslash";
    case XK_bracketright: return "bracket_right";
    case XK_asciicircum: return "6";
    case XK_underscore: return "minus";
    case XK_grave: return "grave_accent";
    case XK_braceleft: return "bracket_left";
    case XK_bar: return "backslash";
    case XK_braceright: return "bracket_right";
    case XK_asciitilde: return "grave_accent";
    case XK_BackSpace: return "backspace";
    case XK_Tab:
    case XK_ISO_Left_Tab: return "tab";
    case XK_Return: return "ret";
    case XK_Escape: return "esc";
    case XK_Insert: return "insert";
    case XK_Delete: return "delete";
    case XK_Home: return "home";
    case XK_End: return "end";
    case XK_Page_Up: return "pgup";
    case XK_Page_Down: return "pgdn";
    case XK_Left: return "left";
    case XK_Right: return "right";
    case XK_Up: return "up";
    case XK_Down: return "down";
    case XK_Shift_L: return "shift";
    case XK_Shift_R: return "shift_r";
    case XK_Control_L: return "ctrl";
    case XK_Control_R: return "ctrl_r";
    case XK_Alt_L: return "alt";
    case XK_Alt_R: return "alt_r";
    case XK_Meta_L:
    case XK_Super_L: return "meta_l";
    case XK_Meta_R:
    case XK_Super_R: return "meta_r";
    case XK_Menu: return "menu";
    case XK_Caps_Lock: return "caps_lock";
    case XK_Num_Lock: return "num_lock";
    case XK_Scroll_Lock: return "scroll_lock";
    case XK_Print: return "print";
    case XK_Pause: return "pause";
    case XK_KP_0: return "kp_0";
    case XK_KP_1: return "kp_1";
    case XK_KP_2: return "kp_2";
    case XK_KP_3: return "kp_3";
    case XK_KP_4: return "kp_4";
    case XK_KP_5: return "kp_5";
    case XK_KP_6: return "kp_6";
    case XK_KP_7: return "kp_7";
    case XK_KP_8: return "kp_8";
    case XK_KP_9: return "kp_9";
    case XK_KP_Decimal: return "kp_decimal";
    case XK_KP_Divide: return "kp_divide";
    case XK_KP_Multiply: return "kp_multiply";
    case XK_KP_Subtract: return "kp_subtract";
    case XK_KP_Add: return "kp_add";
    case XK_KP_Enter: return "kp_enter";
    case XK_KP_Equal: return "kp_equals";
    default: return NULL;
    }
}

static bool astra_keysym_requires_shift(rfbKeySym symbol)
{
    if (symbol >= XK_A && symbol <= XK_Z)
        return true;
    switch (symbol) {
    case XK_exclam:
    case XK_quotedbl:
    case XK_numbersign:
    case XK_dollar:
    case XK_percent:
    case XK_ampersand:
    case XK_parenleft:
    case XK_parenright:
    case XK_asterisk:
    case XK_plus:
    case XK_colon:
    case XK_less:
    case XK_greater:
    case XK_question:
    case XK_at:
    case XK_asciicircum:
    case XK_underscore:
    case XK_braceleft:
    case XK_bar:
    case XK_braceright:
    case XK_asciitilde:
        return true;
    default:
        return false;
    }
}

static void astra_qmp_close(struct astra_remote_desktop *desktop)
{
    if (desktop->qmp != NULL) {
        (void)fclose(desktop->qmp);
        desktop->qmp = NULL;
    }
}

static int astra_qmp_response(struct astra_remote_desktop *desktop,
                              const char *required)
{
    char *line = NULL;
    size_t capacity = 0;
    int result = -1;

    while (getline(&line, &capacity, desktop->qmp) >= 0) {
        if (strstr(line, required) != NULL) {
            result = 0;
            break;
        }
        if (strstr(line, "\"event\"") != NULL)
            continue;
        if (strstr(line, "\"error\"") != NULL) {
            fprintf(stderr, "Astra remote desktop: QMP error: %s", line);
            break;
        }
    }
    free(line);
    return result;
}

static int astra_qmp_connect(struct astra_remote_desktop *desktop)
{
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    int descriptor;

    if (desktop->qmp != NULL)
        return 0;
    if (strlen(desktop->qmp_path) >= sizeof(address.sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(address.sun_path, desktop->qmp_path,
           strlen(desktop->qmp_path) + 1u);
    descriptor = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (descriptor < 0 ||
        connect(descriptor, (struct sockaddr *)&address, sizeof(address)) < 0) {
        if (descriptor >= 0)
            (void)close(descriptor);
        return -1;
    }
    desktop->qmp = fdopen(descriptor, "r+");
    if (desktop->qmp == NULL) {
        (void)close(descriptor);
        return -1;
    }
    if (astra_qmp_response(desktop, "\"QMP\"") != 0 ||
        fprintf(desktop->qmp,
                "{\"execute\":\"qmp_capabilities\"}\n") < 0 ||
        fflush(desktop->qmp) != 0 ||
        astra_qmp_response(desktop, "\"return\"") != 0) {
        astra_qmp_close(desktop);
        return -1;
    }
    return 0;
}

static int astra_qmp_word_parse(const char *line, uint32_t *value)
{
    const char *marker;
    char *end;
    unsigned long parsed;

    if (line == NULL || value == NULL ||
        (marker = strstr(line, ": 0x")) == NULL)
        return -1;
    errno = 0;
    parsed = strtoul(marker + 2, &end, 16);
    if (errno != 0 || end == marker + 2 || parsed > UINT32_MAX)
        return -1;
    *value = (uint32_t)parsed;
    return 0;
}

static int astra_qmp_input_level(struct astra_remote_desktop *desktop,
                                 unsigned int *level)
{
    char *line = NULL;
    size_t capacity = 0u;
    uint32_t status;
    int result = -1;

    if (astra_qmp_connect(desktop) != 0 ||
        fprintf(desktop->qmp,
                "{\"execute\":\"human-monitor-command\",\"arguments\":"
                "{\"command-line\":\"xp /1xw 0x%08x\"}}\n",
                ASTRA_INPUT_STATUS_ADDRESS) < 0 ||
        fflush(desktop->qmp) != 0)
        goto failed;
    while (getline(&line, &capacity, desktop->qmp) >= 0) {
        if (strstr(line, "\"event\"") != NULL)
            continue;
        if (strstr(line, "\"return\"") != NULL &&
            astra_qmp_word_parse(line, &status) == 0) {
            *level = status & ASTRA_INPUT_LEVEL_MASK;
            result = 0;
        } else if (strstr(line, "\"error\"") != NULL) {
            fprintf(stderr, "Astra remote desktop: QMP error: %s", line);
        }
        break;
    }
    if (result == 0) {
        free(line);
        return 0;
    }

failed:
    free(line);
    astra_qmp_close(desktop);
    return -1;
}

static bool astra_remote_input_has_room(unsigned int level,
                                        unsigned int events)
{
    return events <= ASTRA_INPUT_LEVEL_MASK &&
           level <= ASTRA_INPUT_LEVEL_MASK - events;
}

static int astra_qmp_wait_input(struct astra_remote_desktop *desktop,
                                unsigned int events, bool empty)
{
    unsigned int level;

    while (astra_remote_running) {
        if (astra_qmp_input_level(desktop, &level) != 0)
            return -1;
        if ((empty && level == 0u) ||
            (!empty && astra_remote_input_has_room(level, events)))
            return 0;
        if (poll(NULL, 0u, 1) < 0 && errno != EINTR)
            return -1;
    }
    errno = EINTR;
    return -1;
}

static int astra_qmp_key(struct astra_remote_desktop *desktop,
                         const char *qcode, bool down)
{
    if (astra_qmp_connect(desktop) != 0 ||
        astra_qmp_wait_input(desktop, 1u, false) != 0)
        return -1;
    if (fprintf(desktop->qmp,
                "{\"execute\":\"input-send-event\",\"arguments\":"
                "{\"events\":[{\"type\":\"key\",\"data\":{"
                "\"down\":%s,\"key\":{\"type\":\"qcode\","
                "\"data\":\"%s\"}}}]}}\n",
                down ? "true" : "false", qcode) < 0 ||
        fflush(desktop->qmp) != 0 ||
        astra_qmp_response(desktop, "\"return\"") != 0 ||
        astra_qmp_wait_input(desktop, 0u, true) != 0) {
        astra_qmp_close(desktop);
        return -1;
    }
    return 0;
}

static struct astra_remote_key *astra_remote_key_find(
    struct astra_remote_desktop *desktop, const char *qcode)
{
    struct astra_remote_key *key;

    for (key = desktop->keys; key != NULL; key = key->next) {
        if (strcmp(key->qcode, qcode) == 0)
            return key;
    }
    return NULL;
}

static struct astra_remote_key *astra_remote_key_get(
    struct astra_remote_desktop *desktop, const char *qcode)
{
    struct astra_remote_key *key = astra_remote_key_find(desktop, qcode);

    if (key != NULL)
        return key;
    key = calloc(1u, sizeof(*key));
    if (key != NULL) {
        key->qcode = qcode;
        key->next = desktop->keys;
        desktop->keys = key;
    }
    return key;
}

static void astra_remote_reconcile_keys(struct astra_remote_desktop *desktop)
{
    struct astra_remote_key **link = &desktop->keys;

    while (*link != NULL) {
        struct astra_remote_key *key = *link;
        bool wanted = key->references != 0u;

        if (wanted != key->sent_down &&
            astra_qmp_key(desktop, key->qcode, wanted) == 0)
            key->sent_down = wanted;
        if (key->references == 0u && !key->sent_down) {
            *link = key->next;
            free(key);
        } else {
            link = &key->next;
        }
    }
}

static void astra_remote_key_event(rfbBool down, rfbKeySym symbol,
                                   rfbClientPtr client)
{
    struct astra_remote_desktop *desktop = client->screen->screenData;
    struct astra_remote_client *state = client->clientData;
    struct astra_remote_client_key **link;
    struct astra_remote_client_key *held;
    struct astra_remote_key *key;
    struct astra_remote_key *modifier = NULL;
    const char *qcode = astra_qcode_for_keysym(symbol);

    if (qcode == NULL) {
        fprintf(stderr, "Astra remote desktop: unmapped keysym 0x%08lx\n",
                (unsigned long)symbol);
        return;
    }
    link = &state->keys;
    while (*link != NULL && (*link)->symbol != symbol)
        link = &(*link)->next;
    if (down) {
        if (*link != NULL)
            return;
        key = astra_remote_key_get(desktop, qcode);
        if (astra_keysym_requires_shift(symbol))
            modifier = astra_remote_key_get(desktop, "shift");
        held = malloc(sizeof(*held));
        if (key == NULL ||
            (astra_keysym_requires_shift(symbol) && modifier == NULL) ||
            held == NULL) {
            free(held);
            astra_remote_reconcile_keys(desktop);
            return;
        }
        held->symbol = symbol;
        held->key = key;
        held->modifier = modifier;
        held->next = state->keys;
        state->keys = held;
        if (modifier != NULL)
            ++modifier->references;
        ++key->references;
    } else if (*link != NULL) {
        held = *link;
        *link = held->next;
        --held->key->references;
        astra_remote_reconcile_keys(desktop);
        if (held->modifier != NULL)
            --held->modifier->references;
        free(held);
    }
    astra_remote_reconcile_keys(desktop);
}

static void astra_remote_release_keys(rfbClientPtr client)
{
    struct astra_remote_desktop *desktop = client->screen->screenData;
    struct astra_remote_client *state = client->clientData;

    while (state != NULL && state->keys != NULL) {
        struct astra_remote_client_key *held = state->keys;

        state->keys = held->next;
        --held->key->references;
        astra_remote_reconcile_keys(desktop);
        if (held->modifier != NULL)
            --held->modifier->references;
        free(held);
        astra_remote_reconcile_keys(desktop);
    }
}

static int astra_qmp_pointer(struct astra_remote_desktop *desktop,
                             unsigned int wanted_buttons)
{
    static const char *const button_names[] = { "left", "middle", "right" };
    unsigned int changed = wanted_buttons ^ desktop->sent_buttons;
    unsigned int index;
    unsigned int events = 2u;

    for (index = 0u; index < 3u; ++index)
        if ((changed & (1u << index)) != 0u)
            ++events;

    if (astra_qmp_connect(desktop) != 0 ||
        astra_qmp_wait_input(desktop, events, false) != 0)
        return -1;
    if (fprintf(desktop->qmp,
                "{\"execute\":\"input-send-event\",\"arguments\":{"
                "\"events\":[{\"type\":\"abs\",\"data\":{"
                "\"axis\":\"x\",\"value\":%d}},{\"type\":\"abs\","
                "\"data\":{\"axis\":\"y\",\"value\":%d}}",
                desktop->pointer_x, desktop->pointer_y) < 0)
        goto failed;
    for (index = 0; index < 3u; ++index) {
        unsigned int bit = 1u << index;

        if ((changed & bit) != 0u &&
            fprintf(desktop->qmp,
                    ",{\"type\":\"btn\",\"data\":{\"button\":\"%s\","
                    "\"down\":%s}}",
                    button_names[index],
                    (wanted_buttons & bit) != 0u ? "true" : "false") < 0)
            goto failed;
    }
    if (fprintf(desktop->qmp, "]}}\n") < 0 || fflush(desktop->qmp) != 0 ||
        astra_qmp_response(desktop, "\"return\"") != 0 ||
        astra_qmp_wait_input(desktop, 0u, true) != 0)
        goto failed;
    desktop->sent_buttons = wanted_buttons;
    desktop->pointer_dirty = false;
    return 0;

failed:
    astra_qmp_close(desktop);
    return -1;
}

static void astra_qmp_wheel(struct astra_remote_desktop *desktop,
                            const char *button)
{
    if (astra_qmp_connect(desktop) != 0 ||
        astra_qmp_wait_input(desktop, 2u, false) != 0)
        return;
    if (fprintf(desktop->qmp,
                "{\"execute\":\"input-send-event\",\"arguments\":{"
                "\"events\":[{\"type\":\"btn\",\"data\":{"
                "\"button\":\"%s\",\"down\":true}},{\"type\":\"btn\","
                "\"data\":{\"button\":\"%s\",\"down\":false}}]}}\n",
                button, button) < 0 || fflush(desktop->qmp) != 0 ||
        astra_qmp_response(desktop, "\"return\"") != 0 ||
        astra_qmp_wait_input(desktop, 0u, true) != 0)
        astra_qmp_close(desktop);
}

static unsigned int astra_remote_wanted_buttons(
    const struct astra_remote_desktop *desktop)
{
    unsigned int buttons = 0;
    unsigned int index;

    for (index = 0; index < 3u; ++index) {
        if (desktop->button_references[index] != 0u)
            buttons |= 1u << index;
    }
    return buttons;
}

static bool astra_remote_button_transition(unsigned int previous,
                                            unsigned int current)
{
    return ((previous ^ current) & 7u) != 0u;
}

static void astra_remote_pointer_event(int mask, int x, int y,
                                       rfbClientPtr client)
{
    struct astra_remote_desktop *desktop = client->screen->screenData;
    struct astra_remote_client *state = client->clientData;
    unsigned int current = (unsigned int)mask;
    unsigned int buttons = current & 7u;
    unsigned int changed = buttons ^ (state->buttons & 7u);
    bool button_transition = astra_remote_button_transition(state->buttons,
                                                            current);
    unsigned int index;

    desktop->pointer_x = x < 0 ? 0 :
        (x >= (int)ASTRA_DISPLAY_CAPTURE_WIDTH ?
         (int)ASTRA_DISPLAY_CAPTURE_WIDTH - 1 : x);
    desktop->pointer_y = y < 0 ? 0 :
        (y >= (int)ASTRA_DISPLAY_CAPTURE_HEIGHT ?
         (int)ASTRA_DISPLAY_CAPTURE_HEIGHT - 1 : y);
    desktop->pointer_dirty = true;
    for (index = 0; index < 3u; ++index) {
        unsigned int bit = 1u << index;

        if ((changed & bit) == 0u)
            continue;
        if ((buttons & bit) != 0u)
            ++desktop->button_references[index];
        else
            --desktop->button_references[index];
    }
    if ((current & 8u) != 0u && (state->buttons & 8u) == 0u)
        astra_qmp_wheel(desktop, "wheel-up");
    if ((current & 16u) != 0u && (state->buttons & 16u) == 0u)
        astra_qmp_wheel(desktop, "wheel-down");
    state->buttons = current;
    if (button_transition)
        (void)astra_qmp_pointer(desktop,
                                astra_remote_wanted_buttons(desktop));
    rfbDefaultPtrAddEvent(mask, desktop->pointer_x, desktop->pointer_y,
                          client);
}

static void astra_remote_client_gone(rfbClientPtr client)
{
    struct astra_remote_desktop *desktop = client->screen->screenData;
    struct astra_remote_client *state = client->clientData;
    unsigned int index;
    bool released_button = false;

    astra_remote_release_keys(client);
    if (state != NULL) {
        for (index = 0; index < 3u; ++index) {
            if ((state->buttons & (1u << index)) != 0u) {
                --desktop->button_references[index];
                released_button = true;
            }
        }
        free(state);
        client->clientData = NULL;
    }
    if (released_button) {
        desktop->pointer_dirty = true;
        (void)astra_qmp_pointer(desktop,
                                astra_remote_wanted_buttons(desktop));
    }
}

static enum rfbNewClientAction astra_remote_client_new(rfbClientPtr client)
{
    struct astra_remote_client *state = calloc(1u, sizeof(*state));

    if (state == NULL)
        return RFB_CLIENT_REFUSE;
    client->clientData = state;
    client->clientGoneHook = astra_remote_client_gone;
    return RFB_CLIENT_ACCEPT;
}

static int astra_remote_network_config(const char *address_text,
                                       const char *password,
                                       in_addr_t *listen_interface)
{
    struct in_addr address;
    const char *text = address_text != NULL && *address_text != '\0' ?
        address_text : "127.0.0.1";
    size_t password_length = password != NULL ? strlen(password) : 0u;

    if (inet_pton(AF_INET, text, &address) != 1 ||
        password_length > ASTRA_RFB_PASSWORD_MAX) {
        errno = EINVAL;
        return -1;
    }
    if (address.s_addr != htonl(INADDR_LOOPBACK) && password_length == 0u) {
        errno = EACCES;
        return -1;
    }
    *listen_interface = address.s_addr;
    return 0;
}

static int astra_remote_password_parse(
    const char *input, char password[ASTRA_RFB_PASSWORD_MAX + 1u])
{
    size_t length = strcspn(input, "\r\n");

    if (length == 0u || length > ASTRA_RFB_PASSWORD_MAX ||
        (input[length] == '\0' &&
         length == ASTRA_RFB_PASSWORD_MAX + 1u)) {
        errno = EINVAL;
        return -1;
    }
    memcpy(password, input, length);
    password[length] = '\0';
    return 0;
}

static int astra_remote_password_read(const char *path,
                                      char password[ASTRA_RFB_PASSWORD_MAX + 1u])
{
    char input[ASTRA_RFB_PASSWORD_MAX + 2u];
    FILE *file;

    password[0] = '\0';
    if (path == NULL || *path == '\0')
        return 0;
    file = fopen(path, "r");
    if (file == NULL)
        return -1;
    if (fgets(input, sizeof(input), file) == NULL) {
        (void)fclose(file);
        return -1;
    }
    if (fclose(file) != 0)
        return -1;
    return astra_remote_password_parse(input, password);
}

static void astra_remote_pixel_format(rfbPixelFormat *format)
{
    format->bitsPerPixel = 24;
    format->depth = 24;
    format->bigEndian = 0u;
    format->trueColour = 1u;
    format->redMax = 255;
    format->greenMax = 255;
    format->blueMax = 255;
    format->redShift = 0;
    format->greenShift = 8;
    format->blueShift = 16;
}

static bool astra_remote_damage_find(const uint8_t *current,
                                     const uint8_t *previous,
                                     unsigned int width,
                                     unsigned int height,
                                     struct astra_remote_damage *damage)
{
    bool changed = false;
    unsigned int x;
    unsigned int y;

    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            size_t offset = ((size_t)y * width + x) * 3u;

            if (current[offset] == previous[offset] &&
                current[offset + 1u] == previous[offset + 1u] &&
                current[offset + 2u] == previous[offset + 2u])
                continue;
            if (!changed) {
                damage->x1 = x;
                damage->y1 = y;
                damage->x2 = x + 1u;
                damage->y2 = y + 1u;
                changed = true;
            } else {
                if (x < damage->x1)
                    damage->x1 = x;
                if (y < damage->y1)
                    damage->y1 = y;
                if (x >= damage->x2)
                    damage->x2 = x + 1u;
                if (y >= damage->y2)
                    damage->y2 = y + 1u;
            }
        }
    }
    return changed;
}

static int astra_remote_self_test(void)
{
    in_addr_t listen_interface;
    char password[ASTRA_RFB_PASSWORD_MAX + 1u];
    rfbPixelFormat format = {0};
    struct astra_remote_damage damage;
    uint8_t previous[18] = {0};
    uint8_t current[18] = {0};
    uint32_t input_status;

    astra_remote_pixel_format(&format);
    current[(1u * 3u + 2u) * 3u] = 1u;

    if (strcmp(astra_qcode_for_keysym(XK_A), "a") != 0 ||
        strcmp(astra_qcode_for_keysym(XK_exclam), "1") != 0 ||
        !astra_keysym_requires_shift(XK_A) ||
        !astra_keysym_requires_shift(XK_exclam) ||
        astra_keysym_requires_shift(XK_a) ||
        astra_keysym_requires_shift(XK_1) ||
        strcmp(astra_qcode_for_keysym(XK_F24), "f24") != 0 ||
        strcmp(astra_qcode_for_keysym(XK_KP_Enter), "kp_enter") != 0 ||
        astra_qcode_for_keysym(0x0101f642u) != NULL ||
        astra_qmp_word_parse(
            "{\"return\":\"00000000fff0070c: 0x0000001f\\r\\n\"}",
            &input_status) != 0 || input_status != 31u ||
        astra_qmp_word_parse("{\"return\":\"not a word\"}",
                            &input_status) == 0 ||
        !astra_remote_input_has_room(30u, 1u) ||
        astra_remote_input_has_room(31u, 1u) ||
        !astra_remote_input_has_room(29u, 2u) ||
        astra_remote_input_has_room(30u, 2u) ||
        astra_remote_network_config(NULL, NULL, &listen_interface) != 0 ||
        listen_interface != htonl(INADDR_LOOPBACK) ||
        astra_remote_network_config("0.0.0.0", "Astra68!",
                                    &listen_interface) != 0 ||
        listen_interface != htonl(INADDR_ANY) ||
        astra_remote_network_config("0.0.0.0", NULL,
                                    &listen_interface) == 0 ||
        astra_remote_network_config("not-an-address", "Astra68!",
                                    &listen_interface) == 0 ||
        astra_remote_network_config("0.0.0.0", "123456789",
                                    &listen_interface) == 0 ||
        astra_remote_password_parse("Astra68!\n", password) != 0 ||
        strcmp(password, "Astra68!") != 0 ||
        astra_remote_password_parse("\n", password) == 0 ||
        astra_remote_password_parse("123456789", password) == 0 ||
        format.bitsPerPixel != 24 || format.depth != 24 ||
        format.bigEndian != 0u || format.trueColour != 1u ||
        format.redMax != 255 || format.greenMax != 255 ||
        format.blueMax != 255 || format.redShift != 0 ||
        format.greenShift != 8 || format.blueShift != 16 ||
        astra_remote_button_transition(0u, 0u) ||
        astra_remote_button_transition(0u, 8u) ||
        !astra_remote_button_transition(0u, 1u) ||
        !astra_remote_button_transition(1u, 0u) ||
        astra_remote_damage_find(previous, previous, 3u, 2u,
                                 &damage) ||
        !astra_remote_damage_find(current, previous, 3u, 2u, &damage) ||
        damage.x1 != 2u || damage.y1 != 1u ||
        damage.x2 != 3u || damage.y2 != 2u)
        return EXIT_FAILURE;
    puts("ASTRA_REMOTE_DESKTOP_SELF_TEST PASS");
    return EXIT_SUCCESS;
}

static void astra_remote_server_stop(struct astra_remote_server *server)
{
    if (server->screen != NULL) {
        rfbShutdownServer(server->screen, TRUE);
        rfbScreenCleanup(server->screen);
        server->screen = NULL;
    }
    astra_qmp_close(&server->desktop);
    while (server->desktop.keys != NULL) {
        struct astra_remote_key *next = server->desktop.keys->next;

        free(server->desktop.keys);
        server->desktop.keys = next;
    }
    memset(server->desktop.button_references, 0,
           sizeof(server->desktop.button_references));
    server->desktop.sent_buttons = 0u;
    server->desktop.pointer_dirty = false;
    free(server->previous_frame);
    server->previous_frame = NULL;
    server->have_frame = false;
    if (server->frame != MAP_FAILED) {
        (void)munmap((void *)(uintptr_t)server->frame,
                     ASTRA_DISPLAY_CAPTURE_FRAME_BYTES);
        server->frame = MAP_FAILED;
    }
    if (server->capture >= 0) {
        (void)close(server->capture);
        server->capture = -1;
    }
}

static int astra_remote_server_start(struct astra_remote_server *server,
                                     const char *capture_path, int port,
                                     in_addr_t listen_interface,
                                     const char *listen_address,
                                     const char *password, const char *program)
{
    int rfb_argc = 1;
    char *rfb_argv[] = {(char *)(uintptr_t)program, NULL};

    server->capture = open(capture_path, O_RDONLY | O_CLOEXEC);
    if (server->capture < 0) {
        perror("open Astra display capture");
        return -1;
    }
    server->frame = mmap(NULL, ASTRA_DISPLAY_CAPTURE_FRAME_BYTES, PROT_READ,
                         MAP_SHARED, server->capture, 0);
    if (server->frame == MAP_FAILED) {
        perror("map Astra display capture");
        astra_remote_server_stop(server);
        return -1;
    }
    server->previous_frame = malloc(ASTRA_DISPLAY_CAPTURE_FRAME_BYTES);
    if (server->previous_frame == NULL) {
        astra_remote_server_stop(server);
        return -1;
    }
    server->screen = rfbGetScreen(&rfb_argc, rfb_argv,
                                  ASTRA_DISPLAY_CAPTURE_WIDTH,
                                  ASTRA_DISPLAY_CAPTURE_HEIGHT, 8, 3, 3);
    if (server->screen == NULL) {
        fputs("Astra remote desktop: cannot create RFB server\n", stderr);
        astra_remote_server_stop(server);
        return -1;
    }
    server->screen->screenData = &server->desktop;
    server->screen->frameBuffer = (char *)(uintptr_t)server->frame;
    server->screen->desktopName = "Astra 68";
    /* The captured frame already contains Astra's hardware pointer. */
    rfbSetCursor(server->screen, NULL);
    server->screen->port = port;
    server->screen->ipv6port = 0;
    server->screen->listenInterface = listen_interface;
    if (password != NULL && *password != '\0') {
        server->passwords[0] = (char *)(uintptr_t)password;
        server->passwords[1] = NULL;
        server->screen->authPasswdData = server->passwords;
        server->screen->passwordCheck = rfbCheckPasswordByList;
    }
    server->screen->alwaysShared = TRUE;
    server->screen->handleEventsEagerly = TRUE;
    server->screen->kbdAddEvent = astra_remote_key_event;
    server->screen->kbdReleaseAllKeys = astra_remote_release_keys;
    server->screen->ptrAddEvent = astra_remote_pointer_event;
    server->screen->newClientHook = astra_remote_client_new;
    astra_remote_pixel_format(&server->screen->serverFormat);
    rfbInitServer(server->screen);
    if (!rfbIsActive(server->screen)) {
        fputs("Astra remote desktop: cannot start RFB server\n", stderr);
        astra_remote_server_stop(server);
        return -1;
    }
    if (++server->generation == 0u)
        ++server->generation;
    (void)fprintf(stderr,
                  "Astra remote desktop: ready generation %u on %s:%d\n",
                  server->generation, listen_address, port);
    return 0;
}

static int astra_control_listen(const char *path)
{
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    struct stat status;
    int descriptor;

    if (strlen(path) >= sizeof(address.sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (lstat(path, &status) == 0) {
        if (!S_ISSOCK(status.st_mode)) {
            errno = EEXIST;
            return -1;
        }
        if (unlink(path) != 0)
            return -1;
    } else if (errno != ENOENT) {
        return -1;
    }
    descriptor = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (descriptor < 0)
        return -1;
    memcpy(address.sun_path, path, strlen(path) + 1u);
    if (bind(descriptor, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        chmod(path, 0600) != 0 || listen(descriptor, 1) != 0) {
        int saved = errno;

        (void)close(descriptor);
        (void)unlink(path);
        errno = saved;
        return -1;
    }
    return descriptor;
}

static int astra_control_accept(int listener)
{
    int descriptor = accept(listener, NULL, NULL);

    if (descriptor >= 0 && fcntl(descriptor, F_SETFD, FD_CLOEXEC) != 0) {
        int saved = errno;

        (void)close(descriptor);
        errno = saved;
        return -1;
    }
    return descriptor;
}

static int astra_control_closed(int descriptor)
{
    struct pollfd event = {.fd = descriptor, .events = POLLIN | POLLHUP};
    char byte;
    int ready = poll(&event, 1u, 0);

    if (ready < 0)
        return errno != EINTR;
    if (ready == 0)
        return 0;
    if ((event.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
        return 1;
    return (event.revents & POLLIN) != 0 &&
           recv(descriptor, &byte, sizeof(byte), MSG_PEEK) != -1;
}

static int astra_port_from_environment(const char *text, int *port)
{
    char *end = NULL;
    long value;

    if (text == NULL || *text == '\0') {
        *port = ASTRA_RFB_PORT;
        return 0;
    }
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        value < 1 || value > 65535) {
        errno = EINVAL;
        return -1;
    }
    *port = (int)value;
    return 0;
}

int main(int argc, char **argv)
{
    const char *qmp_environment = getenv("ASTRA_QMP_SOCKET");
    const char *capture_environment = getenv("ASTRA_CAPTURE_DEVICE");
    const char *control_environment = getenv(
        "ASTRA_REMOTE_DESKTOP_CONTROL_SOCKET");
    const char *listen_environment = getenv("ASTRA_RFB_LISTEN_ADDRESS");
    const char *password_path = getenv("ASTRA_RFB_PASSWORD_FILE");
    char password_storage[ASTRA_RFB_PASSWORD_MAX + 1u] = {0};
    const char *password;
    const char *listen_address =
        listen_environment != NULL && *listen_environment != '\0' ?
            listen_environment : "127.0.0.1";
    const char *capture_path = capture_environment != NULL ?
        capture_environment : ASTRA_CAPTURE_DEVICE;
    const char *control_path = control_environment != NULL ?
        control_environment : ASTRA_CONTROL_DEFAULT;
    struct astra_remote_server server = {
        .desktop = {
            .qmp_path = qmp_environment != NULL ? qmp_environment :
                                                       ASTRA_QMP_DEFAULT,
        },
        .frame = MAP_FAILED,
        .capture = -1,
    };
    int listener = -1;
    int lease = -1;
    int port;
    int status = EXIT_FAILURE;
    in_addr_t listen_interface;

    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
        return astra_remote_self_test();
    if (argc != 1) {
        fprintf(stderr, "usage: %s [--self-test]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (astra_port_from_environment(getenv("ASTRA_RFB_PORT"), &port) != 0) {
        fputs("Astra remote desktop: invalid ASTRA_RFB_PORT\n", stderr);
        goto done;
    }
    if (astra_remote_password_read(password_path, password_storage) != 0) {
        fputs("Astra remote desktop: invalid RFB password file\n", stderr);
        goto done;
    }
    password = password_storage[0] != '\0' ? password_storage : NULL;
    if (astra_remote_network_config(listen_environment, password,
                                    &listen_interface) != 0) {
        fputs("Astra remote desktop: invalid or insecure RFB network "
              "configuration\n", stderr);
        goto done;
    }
    listener = astra_control_listen(control_path);
    if (listener < 0) {
        perror("listen for Astra remote desktop control");
        goto done;
    }
    signal(SIGINT, astra_remote_stop);
    signal(SIGTERM, astra_remote_stop);
    signal(SIGPIPE, SIG_IGN);
    status = EXIT_SUCCESS;
    while (astra_remote_running) {
        if (lease < 0) {
            struct pollfd event = {.fd = listener, .events = POLLIN};
            int ready = poll(&event, 1u, -1);

            if (ready < 0) {
                if (errno == EINTR)
                    continue;
                perror("wait for Astra remote desktop control");
                status = EXIT_FAILURE;
                break;
            }
            lease = astra_control_accept(listener);
            if (lease < 0) {
                if (errno != EINTR)
                    perror("accept Astra remote desktop control");
                continue;
            }
            if (astra_remote_server_start(&server, capture_path, port,
                                          listen_interface, listen_address,
                                          password, argv[0]) != 0) {
                int failure = errno != 0 ? errno : EIO;
                char reply[32];
                int length = snprintf(reply, sizeof(reply), "ERROR %d\n",
                                      failure);

                if (length > 0)
                    (void)send(lease, reply, (size_t)length, MSG_NOSIGNAL);
                (void)close(lease);
                lease = -1;
                continue;
            }
            {
                char reply[32];
                int length = snprintf(reply, sizeof(reply), "READY %u\n",
                                      server.generation);

                if (length <= 0 ||
                    send(lease, reply, (size_t)length, MSG_NOSIGNAL) !=
                        length) {
                    astra_remote_server_stop(&server);
                    (void)close(lease);
                    lease = -1;
                    continue;
                }
            }
        }
        rfbProcessEvents(
            server.screen,
            server.screen->clientHead == NULL ? ASTRA_RFB_IDLE_US : 0);
        astra_remote_reconcile_keys(&server.desktop);
        if (server.desktop.pointer_dirty)
            (void)astra_qmp_pointer(
                &server.desktop,
                astra_remote_wanted_buttons(&server.desktop));
        if (astra_control_closed(lease)) {
            (void)fprintf(stderr,
                          "Astra remote desktop: lease released generation "
                          "%u\n", server.generation);
            astra_remote_server_stop(&server);
            (void)close(lease);
            lease = -1;
            continue;
        }
        if (server.screen->clientHead == NULL)
            continue;
        {
            struct astra_display_capture_info info;
            struct astra_remote_damage damage;

            if (ioctl(server.capture, ASTRA_DISPLAY_CAPTURE_IOC_CAPTURE,
                      &info) != 0) {
            perror("capture Astra display");
                astra_remote_server_stop(&server);
                (void)close(lease);
                lease = -1;
                continue;
            }
            if (info.width != ASTRA_DISPLAY_CAPTURE_WIDTH ||
                info.height != ASTRA_DISPLAY_CAPTURE_HEIGHT ||
                info.frame_bytes != ASTRA_DISPLAY_CAPTURE_FRAME_BYTES) {
                fputs("Astra remote desktop: invalid capture contract\n",
                      stderr);
                astra_remote_server_stop(&server);
                (void)close(lease);
                lease = -1;
                continue;
            }
            if (!server.have_frame) {
                damage.x1 = 0u;
                damage.y1 = 0u;
                damage.x2 = info.width;
                damage.y2 = info.height;
            } else if (!astra_remote_damage_find(
                           server.frame, server.previous_frame,
                           info.width, info.height, &damage)) {
                continue;
            }
            memcpy(server.previous_frame, server.frame, info.frame_bytes);
            server.have_frame = true;
            rfbMarkRectAsModified(server.screen, damage.x1, damage.y1,
                                  damage.x2, damage.y2);
        }
    }

done:
    astra_remote_server_stop(&server);
    if (lease >= 0)
        (void)close(lease);
    if (listener >= 0) {
        (void)close(listener);
        (void)unlink(control_path);
    }
    return status;
}
