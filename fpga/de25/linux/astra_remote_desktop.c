// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L

#include <astra/display_capture.h>

#include "astra_display_capture_uapi.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
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
#include <sys/un.h>
#include <unistd.h>

#define ASTRA_CAPTURE_DEVICE "/dev/astra-display-capture"
#define ASTRA_QMP_DEFAULT "/run/astra/remote-desktop-qmp.sock"
#define ASTRA_RFB_PORT 5900
#define ASTRA_RFB_IDLE_US 100000L

struct astra_remote_key {
    const char *qcode;
    unsigned int references;
    bool sent_down;
    struct astra_remote_key *next;
};

struct astra_remote_client_key {
    struct astra_remote_key *key;
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

static int astra_qmp_key(struct astra_remote_desktop *desktop,
                         const char *qcode, bool down)
{
    if (astra_qmp_connect(desktop) != 0)
        return -1;
    if (fprintf(desktop->qmp,
                "{\"execute\":\"input-send-event\",\"arguments\":"
                "{\"events\":[{\"type\":\"key\",\"data\":{"
                "\"down\":%s,\"key\":{\"type\":\"qcode\","
                "\"data\":\"%s\"}}}]}}\n",
                down ? "true" : "false", qcode) < 0 ||
        fflush(desktop->qmp) != 0 ||
        astra_qmp_response(desktop, "\"return\"") != 0) {
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
    const char *qcode = astra_qcode_for_keysym(symbol);

    if (qcode == NULL) {
        fprintf(stderr, "Astra remote desktop: unmapped keysym 0x%08lx\n",
                (unsigned long)symbol);
        return;
    }
    link = &state->keys;
    while (*link != NULL && strcmp((*link)->key->qcode, qcode) != 0)
        link = &(*link)->next;
    if (down) {
        if (*link != NULL)
            return;
        key = astra_remote_key_find(desktop, qcode);
        if (key == NULL) {
            key = calloc(1u, sizeof(*key));
            if (key == NULL)
                return;
            key->qcode = qcode;
            key->next = desktop->keys;
            desktop->keys = key;
        }
        held = malloc(sizeof(*held));
        if (held == NULL)
            return;
        held->key = key;
        held->next = state->keys;
        state->keys = held;
        ++key->references;
    } else if (*link != NULL) {
        held = *link;
        *link = held->next;
        --held->key->references;
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
        free(held);
    }
    astra_remote_reconcile_keys(desktop);
}

static int astra_qmp_pointer(struct astra_remote_desktop *desktop,
                             unsigned int wanted_buttons)
{
    static const char *const button_names[] = { "left", "middle", "right" };
    unsigned int changed = wanted_buttons ^ desktop->sent_buttons;
    unsigned int index;

    if (astra_qmp_connect(desktop) != 0)
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
        astra_qmp_response(desktop, "\"return\"") != 0)
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
    if (astra_qmp_connect(desktop) != 0)
        return;
    if (fprintf(desktop->qmp,
                "{\"execute\":\"input-send-event\",\"arguments\":{"
                "\"events\":[{\"type\":\"btn\",\"data\":{"
                "\"button\":\"%s\",\"down\":true}},{\"type\":\"btn\","
                "\"data\":{\"button\":\"%s\",\"down\":false}}]}}\n",
                button, button) < 0 || fflush(desktop->qmp) != 0 ||
        astra_qmp_response(desktop, "\"return\"") != 0)
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

static void astra_remote_pointer_event(int mask, int x, int y,
                                       rfbClientPtr client)
{
    struct astra_remote_desktop *desktop = client->screen->screenData;
    struct astra_remote_client *state = client->clientData;
    unsigned int current = (unsigned int)mask;
    unsigned int buttons = current & 7u;
    unsigned int changed = buttons ^ (state->buttons & 7u);
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
    (void)astra_qmp_pointer(desktop, astra_remote_wanted_buttons(desktop));
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

static int astra_remote_self_test(void)
{
    if (strcmp(astra_qcode_for_keysym(XK_A), "a") != 0 ||
        strcmp(astra_qcode_for_keysym(XK_exclam), "1") != 0 ||
        strcmp(astra_qcode_for_keysym(XK_F24), "f24") != 0 ||
        strcmp(astra_qcode_for_keysym(XK_KP_Enter), "kp_enter") != 0 ||
        astra_qcode_for_keysym(0x0101f642u) != NULL)
        return EXIT_FAILURE;
    puts("ASTRA_REMOTE_DESKTOP_SELF_TEST PASS");
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    struct astra_display_capture_info info;
    const char *qmp_environment = getenv("ASTRA_QMP_SOCKET");
    struct astra_remote_desktop desktop = {
        .qmp_path = qmp_environment != NULL ? qmp_environment : ASTRA_QMP_DEFAULT,
    };
    rfbScreenInfoPtr screen = NULL;
    const uint8_t *frame = MAP_FAILED;
    int capture = -1;
    int rfb_argc = 1;
    char *rfb_argv[] = { argv[0], NULL };
    int status = EXIT_FAILURE;

    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
        return astra_remote_self_test();
    if (argc != 1) {
        fprintf(stderr, "usage: %s [--self-test]\n", argv[0]);
        return EXIT_FAILURE;
    }
    capture = open(ASTRA_CAPTURE_DEVICE, O_RDONLY | O_CLOEXEC);
    if (capture < 0) {
        perror("open Astra display capture");
        goto done;
    }
    frame = mmap(NULL, ASTRA_DISPLAY_CAPTURE_FRAME_BYTES, PROT_READ,
                 MAP_SHARED, capture, 0);
    if (frame == MAP_FAILED) {
        perror("map Astra display capture");
        goto done;
    }
    screen = rfbGetScreen(&rfb_argc, rfb_argv,
                          ASTRA_DISPLAY_CAPTURE_WIDTH,
                          ASTRA_DISPLAY_CAPTURE_HEIGHT, 8, 3, 3);
    if (screen == NULL) {
        fputs("Astra remote desktop: cannot create RFB server\n", stderr);
        goto done;
    }
    screen->screenData = &desktop;
    screen->frameBuffer = (char *)(uintptr_t)frame;
    screen->desktopName = "Astra 68";
    /* The captured frame already contains Astra's hardware pointer. */
    rfbSetCursor(screen, NULL);
    screen->port = ASTRA_RFB_PORT;
    screen->ipv6port = 0;
    screen->listenInterface = htonl(INADDR_LOOPBACK);
    screen->alwaysShared = TRUE;
    screen->kbdAddEvent = astra_remote_key_event;
    screen->kbdReleaseAllKeys = astra_remote_release_keys;
    screen->ptrAddEvent = astra_remote_pointer_event;
    screen->newClientHook = astra_remote_client_new;
    screen->serverFormat.bitsPerPixel = 24;
    screen->serverFormat.depth = 24;
    screen->serverFormat.bigEndian = 1u;
    screen->serverFormat.trueColour = 1u;
    screen->serverFormat.redMax = 255;
    screen->serverFormat.greenMax = 255;
    screen->serverFormat.blueMax = 255;
    screen->serverFormat.redShift = 16;
    screen->serverFormat.greenShift = 8;
    screen->serverFormat.blueShift = 0;
    signal(SIGINT, astra_remote_stop);
    signal(SIGTERM, astra_remote_stop);
    signal(SIGPIPE, SIG_IGN);
    rfbInitServer(screen);
    while (astra_remote_running && rfbIsActive(screen)) {
        rfbProcessEvents(screen,
                         screen->clientHead == NULL ? ASTRA_RFB_IDLE_US : 0);
        astra_remote_reconcile_keys(&desktop);
        if (desktop.pointer_dirty)
            (void)astra_qmp_pointer(
                &desktop, astra_remote_wanted_buttons(&desktop));
        if (screen->clientHead == NULL)
            continue;
        if (ioctl(capture, ASTRA_DISPLAY_CAPTURE_IOC_CAPTURE, &info) != 0) {
            perror("capture Astra display");
            goto done;
        }
        if (info.width != ASTRA_DISPLAY_CAPTURE_WIDTH ||
            info.height != ASTRA_DISPLAY_CAPTURE_HEIGHT ||
            info.frame_bytes != ASTRA_DISPLAY_CAPTURE_FRAME_BYTES) {
            fputs("Astra remote desktop: invalid capture contract\n", stderr);
            goto done;
        }
        rfbMarkRectAsModified(screen, 0, 0, info.width, info.height);
    }
    status = EXIT_SUCCESS;

done:
    if (screen != NULL) {
        rfbShutdownServer(screen, TRUE);
        rfbScreenCleanup(screen);
    }
    astra_qmp_close(&desktop);
    while (desktop.keys != NULL) {
        struct astra_remote_key *next = desktop.keys->next;

        free(desktop.keys);
        desktop.keys = next;
    }
    if (frame != MAP_FAILED)
        (void)munmap((void *)(uintptr_t)frame,
                     ASTRA_DISPLAY_CAPTURE_FRAME_BYTES);
    if (capture >= 0)
        (void)close(capture);
    return status;
}
