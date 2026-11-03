#include <astra/application.h>
#include <astra/config_library.h>
#include <astra/control.h>
#include <astra/fonts.h>
#include <astra/graphics_kit.h>
#include <astra/interface_library.h>
#include <astra/network_library.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/string.h>
#include <astra/scroll.h>
#include <astra/surface.h>
#include <astra/text_model.h>
#include <astra/theme.h>
#include <astra/window.h>

#include <stdint.h>
#include <string.h>

#define SETTINGS_WIDTH 960u
#define SETTINGS_HEIGHT 760u
#define SETTINGS_SIDEBAR_WIDTH 236u
#define SETTINGS_SEARCH_BYTES 96u

enum {
    SETTINGS_ROOT = 1u,
    SETTINGS_SIDEBAR,
    SETTINGS_SIDEBAR_SEARCH,
    SETTINGS_SIDEBAR_SCROLL,
    SETTINGS_SIDEBAR_LIST,
    SETTINGS_CONTENT,
    SETTINGS_TITLE,
    SETTINGS_DESCRIPTION,
    SETTINGS_BODY,
    SETTINGS_ACTIONS,
    SETTINGS_PINS_SCROLL,
    SETTINGS_PINS_LIST,
    SETTINGS_TOOLS,
    SETTINGS_MOVE_UP,
    SETTINGS_MOVE_DOWN,
    SETTINGS_REMOVE,
    SETTINGS_FONTS_BUTTON,
    SETTINGS_ABOUT,
    SETTINGS_STATUS,
    SETTINGS_EMPTY,
    SETTINGS_ETHERNET_SCROLL,
    SETTINGS_ETHERNET_LIST,
    SETTINGS_ETHERNET_IPV4,
    SETTINGS_ETHERNET_IPV6,
    SETTINGS_ETHERNET_REFRESH,
    SETTINGS_ETHERNET_EMPTY,
    SETTINGS_ETHERNET_METHOD,
    SETTINGS_ETHERNET_FORM_FIRST,
    SETTINGS_ETHERNET_FORM_LAST = SETTINGS_ETHERNET_FORM_FIRST + 4u *
        (ASTRA_FORM_ROW_CONTROL_COUNT + 1u) - 1u,
    SETTINGS_ETHERNET_APPLY,
    SETTINGS_ETHERNET_KEEP,
    SETTINGS_ETHERNET_REVERT,
    SETTINGS_ETHERNET_PENDING,
    SETTINGS_CONTROL_COUNT = SETTINGS_ETHERNET_PENDING
};

enum { SETTINGS_DOCK = 1u, SETTINGS_FONTS, SETTINGS_INFO, SETTINGS_ETHERNET };

_Static_assert(SETTINGS_SIDEBAR_LIST == SETTINGS_SIDEBAR +
    ASTRA_SETTINGS_SIDEBAR_LIST &&
    SETTINGS_DESCRIPTION == SETTINGS_TITLE + ASTRA_SETTINGS_PANE_DESCRIPTION &&
    SETTINGS_ACTIONS == SETTINGS_TITLE + ASTRA_SETTINGS_PANE_ACTIONS,
    "Settings uses shared sidebar and pane composition");

ASTRA_PROGRAM("Settings", 0, 1, 0, "Astra68 contributors",
              "Copyright 2026 Astra68 contributors");

static AstraControl controls[SETTINGS_CONTROL_COUNT];
static AstraUIContext ui = ASTRA_UI_CONTEXT_INIT;
static AstraSharedSurface surface;
static AstraWindow window = ASTRA_WINDOW_INIT;
static AstraConfig config = ASTRA_CONFIG_INIT;
static AstraNetworkSession network = ASTRA_NETWORK_SESSION_INIT;
static AstraNetworkSession network_admin = ASTRA_NETWORK_SESSION_INIT;
static const AstraConfigField config_fields[] = {{"pane", 0u}};
static const AstraConfigSchema config_schema = {1u, config_fields, 1u};
static AstraHandle gui;
static AstraHandle launcher;
static uint32_t config_ready;
static uint32_t network_ready;
static uint32_t network_admin_ready;
static uint32_t window_active;
static uint32_t page = SETTINGS_DOCK;
static uint32_t selected_pin;
static char status_text[128];

static AstraTextModel search_model = ASTRA_TEXT_MODEL_INIT;
static char search_content[SETTINGS_SEARCH_BYTES];
static uint32_t search_metadata[256];
static AstraScrollModel sidebar_scroll = ASTRA_SCROLL_MODEL_INIT;
static AstraScrollModel pins_scroll = ASTRA_SCROLL_MODEL_INIT;
static AstraCollectionModel navigation = ASTRA_COLLECTION_MODEL_INIT;
static AstraCollectionModel pin_list = ASTRA_COLLECTION_MODEL_INIT;
static AstraCollectionItem navigation_storage[2][7];
static uint32_t navigation_active;
static AstraCollectionItem pin_items[ASTRA_DOCK_PIN_LIMIT];
static AstraDockPinInfo pins[ASTRA_DOCK_PIN_LIMIT];
static AstraDockPinInfo pin_scratch[ASTRA_DOCK_PIN_LIMIT];
static uint32_t pin_count;
static uint32_t pin_generation;
static AstraScrollModel ethernet_scroll = ASTRA_SCROLL_MODEL_INIT;
static AstraCollectionModel ethernet_list = ASTRA_COLLECTION_MODEL_INIT;
static AstraCollectionItem ethernet_items[16];
static AstraNetworkInterfaceAddress ethernet_addresses[32];
static AstraNetworkInterfaceInfo ethernet_devices[32];
static char ethernet_names[16][ASTRA_NETWORK_INTERFACE_NAME_SIZE];
static char ethernet_ipv4[80] = "IPv4: No address";
static char ethernet_ipv6[110] = "IPv6: No address";
static uint32_t ethernet_address_count;
static uint32_t ethernet_count;
static uint32_t ethernet_selected;
static uint32_t ethernet_method = ASTRA_NETWORK_ETHERNET_IPV4_DHCP;
static AstraTextModel ethernet_models[4];
static char ethernet_content[4][80];
static uint32_t ethernet_metadata[4][128];
static uint64_t ethernet_rollback_at;
static char ethernet_pending[96];

#define CONTROL(id) (&controls[(id) - 1u])

static int contains_folded(const char *haystack, const char *needle,
    uint32_t length)
{
    if (length == 0u) return 1;
    for (uint32_t at = 0u; haystack[at] != '\0'; ++at) {
        uint32_t offset = 0u;
        while (offset < length && haystack[at + offset] != '\0') {
            char left = haystack[at + offset], right = needle[offset];
            if (left >= 'A' && left <= 'Z') left += 'a' - 'A';
            if (right >= 'A' && right <= 'Z') right += 'a' - 'A';
            if (left != right) break;
            ++offset;
        }
        if (offset == length) return 1;
    }
    return 0;
}

static AstraResult refresh_navigation(uint32_t live)
{
    static const char *const names[] = {"Dock", "Fonts", "Ethernet", "About"};
    static const uint32_t ids[] = {
        SETTINGS_DOCK, SETTINGS_FONTS, SETTINGS_ETHERNET, SETTINGS_INFO};
    static const uint32_t icons[] = {
        ASTRA_TOOLBAR_ICON_GRID, ASTRA_TOOLBAR_ICON_LIST,
        ASTRA_TOOLBAR_ICON_HOME, ASTRA_TOOLBAR_ICON_HOME};
    static const char *const headings[] = {"LOOK & FEEL", "NETWORK", "SYSTEM"};
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    char query[SETTINGS_SEARCH_BYTES];
    uint32_t bytes = 0u, count = 0u;
    AstraCollectionItem *items = navigation_storage[
        live != 0u ? 1u - navigation_active : navigation_active];
    if (astra_text_model_get_state(&search_model, &state) != ASTRA_OK ||
        state.text_bytes >= sizeof(query) ||
        astra_text_model_copy(&search_model, 0u, state.text_bytes,
            query, sizeof(query) - 1u, &bytes) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    query[bytes] = '\0';
    for (uint32_t group = 0u; group < 3u; ++group) {
        uint32_t first = group == 0u ? 0u : group + 1u;
        uint32_t last = group == 0u ? 2u : group + 2u;
        uint32_t heading = count;
        for (uint32_t at = first; at < last; ++at) {
            if (!contains_folded(names[at], query, bytes)) continue;
            if (count == heading) {
                const char *text = headings[group];
                items[count++] = (AstraCollectionItem){
                    100u + group, text, (uint32_t)strlen(text),
                    {ASTRA_COLLECTION_HEADING, ASTRA_TOOLBAR_ICON_NONE}};
            }
            items[count++] = (AstraCollectionItem){
                ids[at], names[at], (uint32_t)strlen(names[at]),
                {0u, icons[at]}};
        }
    }
    if (live != 0u) {
        AstraResult result = astra_interface_list_set_items(&ui,
            CONTROL(SETTINGS_SIDEBAR_LIST), items, count);
        if (result != ASTRA_OK) return result;
        navigation_active = 1u - navigation_active;
        for (uint32_t at = 0u; at < count; ++at)
            if (items[at].id == page)
                return astra_interface_control_set_value(&ui,
                    CONTROL(SETTINGS_SIDEBAR_LIST), at);
        return ASTRA_OK;
    }
    navigation.items = items;
    navigation.item_count = count;
    navigation.selected_id = 0u;
    for (uint32_t at = 0u; at < count; ++at)
        if (items[at].id == page) navigation.selected_id = page;
    return ASTRA_OK;
}

static AstraResult refresh_pins(void)
{
    AstraDockPinInfo first = ASTRA_DOCK_PIN_INFO_INIT;
    for (uint32_t attempt = 0u; attempt < 3u; ++attempt) {
        if (astra_dock_pin_query(gui, 0u, &first) != ASTRA_OK ||
            first.count > ASTRA_DOCK_PIN_LIMIT)
            return ASTRA_ERROR_IO;
        if (first.count != 0u) pin_scratch[0] = first;
        uint32_t at = first.count == 0u ? 0u : 1u;
        for (; at < first.count; ++at) {
            pin_scratch[at] = (AstraDockPinInfo)ASTRA_DOCK_PIN_INFO_INIT;
            if (astra_dock_pin_query(gui, at, &pin_scratch[at]) != ASTRA_OK ||
                pin_scratch[at].generation != first.generation ||
                pin_scratch[at].count != first.count)
                break;
        }
        if (at != first.count) continue;
        pin_count = first.count;
        pin_generation = first.generation;
        for (at = 0u; at < pin_count; ++at) {
            pins[at] = pin_scratch[at];
            pin_items[at] = (AstraCollectionItem){at + 1u,
                pins[at].name, (uint32_t)strlen(pins[at].name),
                {0u, ASTRA_TOOLBAR_ICON_GRID}};
        }
        if (pin_count == 0u) selected_pin = 0u;
        else if (selected_pin >= pin_count) selected_pin = pin_count - 1u;
        pin_list.items = pin_items;
        pin_list.item_count = pin_count;
        pin_list.selected_id = pin_count != 0u ? selected_pin + 1u : 0u;
        return ASTRA_OK;
    }
    return ASTRA_ERROR_BUSY;
}

static void ethernet_detail(void)
{
    AstraString text;
    char address[40];
    (void)astra_string_copy(ethernet_ipv4, sizeof(ethernet_ipv4),
        "IPv4: No address");
    (void)astra_string_copy(ethernet_ipv6, sizeof(ethernet_ipv6),
        "IPv6: No address");
    if (ethernet_selected == 0u || ethernet_selected > ethernet_count)
        return;
    for (uint32_t at = 0u; at < ethernet_address_count; ++at) {
        const AstraNetworkInterfaceAddress *record = &ethernet_addresses[at];
        if (strcmp(record->name,
                ethernet_names[ethernet_selected - 1u]) != 0 ||
            astra_network_address_format(&record->address, address,
                sizeof(address)) != ASTRA_NETWORK_OK)
            continue;
        if (record->address.family == ASTRA_NETWORK_FAMILY_IPV4) {
            astra_string_init(&text, ethernet_ipv4, sizeof(ethernet_ipv4));
            (void)astra_string_append(&text, "IPv4: ");
            (void)astra_string_append(&text, address);
        } else if (record->address.family == ASTRA_NETWORK_FAMILY_IPV6) {
            astra_string_init(&text, ethernet_ipv6, sizeof(ethernet_ipv6));
            (void)astra_string_append(&text, "IPv6: ");
            (void)astra_string_append(&text, address);
        }
    }
}

static void refresh_ethernet(void)
{
    uint32_t count = 0u, device_count = 0u;
    char selected_name[ASTRA_NETWORK_INTERFACE_NAME_SIZE] = {0};
    if (ethernet_selected != 0u && ethernet_selected <= ethernet_count)
        (void)astra_string_copy(selected_name, sizeof(selected_name),
                                ethernet_names[ethernet_selected - 1u]);
    ethernet_count = 0u;
    ethernet_address_count = 0u;
    if (network_ready != 0u &&
        astra_network_interfaces(&network, ethernet_addresses,
            sizeof(ethernet_addresses) / sizeof(ethernet_addresses[0]),
            &count) == ASTRA_NETWORK_OK)
        ethernet_address_count = count;
    if (network_ready != 0u &&
        astra_network_devices(&network, ethernet_devices,
            sizeof(ethernet_devices) / sizeof(ethernet_devices[0]),
            &device_count) != ASTRA_NETWORK_OK)
        device_count = 0u;
    for (uint32_t at = 0u; at < device_count; ++at) {
        const char *name = ethernet_devices[at].name;
        if (memchr(name, '\0', ASTRA_NETWORK_INTERFACE_NAME_SIZE) == NULL)
            continue;
        if (ethernet_count == 16u) break;
        (void)astra_string_copy(ethernet_names[ethernet_count],
            sizeof(ethernet_names[ethernet_count]), name);
        ethernet_items[ethernet_count] = (AstraCollectionItem){
            ethernet_count + 1u, ethernet_names[ethernet_count],
            (uint32_t)strlen(ethernet_names[ethernet_count]),
            {0u, ASTRA_TOOLBAR_ICON_HOME}};
        ++ethernet_count;
    }
    ethernet_selected = ethernet_count == 0u ? 0u : 1u;
    for (uint32_t at = 0u; at < ethernet_count; ++at)
        if (strcmp(ethernet_names[at], selected_name) == 0)
            ethernet_selected = at + 1u;
    ethernet_list.items = ethernet_items;
    ethernet_list.item_count = ethernet_count;
    ethernet_list.selected_id = ethernet_selected;
    ethernet_detail();
}

static uint32_t ethernet_form_id(uint32_t index)
{
    return SETTINGS_ETHERNET_FORM_FIRST + index *
        (ASTRA_FORM_ROW_CONTROL_COUNT + 1u);
}

static AstraResult ethernet_text(uint32_t index, char *text, uint32_t capacity)
{
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    uint32_t copied = 0u;
    if (astra_text_model_get_state(&ethernet_models[index], &state) != ASTRA_OK ||
        state.text_bytes >= capacity ||
        astra_text_model_copy(&ethernet_models[index], 0u, state.text_bytes,
            text, capacity - 1u, &copied) != ASTRA_OK ||
        copied != state.text_bytes)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    text[copied] = '\0';
    return ASTRA_OK;
}

static int parse_ethernet_ipv4(const char *text, uint8_t output[4])
{
    AstraNetworkAddress address;
    if (astra_network_address_parse(ASTRA_NETWORK_FAMILY_IPV4, text,
            &address) != ASTRA_NETWORK_OK)
        return 0;
    (void)memcpy(output, address.address, 4u);
    return 1;
}

static int ethernet_candidate(AstraNetworkEthernetIPv4 *candidate)
{
    char text[80];
    uint32_t number = 0u;
    (void)memset(candidate, 0, sizeof(*candidate));
    candidate->size = sizeof(*candidate);
    if (ethernet_selected == 0u || ethernet_selected > ethernet_count)
        return 0;
    (void)astra_string_copy(candidate->name, sizeof(candidate->name),
                            ethernet_names[ethernet_selected - 1u]);
    candidate->method = ethernet_method;
    if (ethernet_method == ASTRA_NETWORK_ETHERNET_IPV4_MANUAL) {
        if (ethernet_text(0u, text, sizeof(text)) != ASTRA_OK ||
            !parse_ethernet_ipv4(text, candidate->address) ||
            ethernet_text(1u, text, sizeof(text)) != ASTRA_OK ||
            text[0] == '\0') return 0;
        for (uint32_t at = 0u; text[at] != '\0'; ++at) {
            if (text[at] < '0' || text[at] > '9' || number > 32u)
                return 0;
            number = number * 10u + (uint32_t)(text[at] - '0');
        }
        candidate->prefix = number;
        if (ethernet_text(2u, text, sizeof(text)) != ASTRA_OK)
            return 0;
        if (text[0] != '\0' && !parse_ethernet_ipv4(text, candidate->gateway))
            return 0;
    }
    if (ethernet_text(3u, text, sizeof(text)) != ASTRA_OK)
        return 0;
    char *part = text;
    for (char *at = text; ; ++at) {
        if (*at != ',' && *at != '\0') continue;
        char separator = *at;
        *at = '\0';
        while (*part == ' ') ++part;
        if (*part != '\0') {
            if (candidate->dns_count == ASTRA_NETWORK_ETHERNET_DNS_MAX ||
                !parse_ethernet_ipv4(part, candidate->dns[candidate->dns_count]))
                return 0;
            ++candidate->dns_count;
        }
        if (separator == '\0') break;
        part = at + 1;
    }
    return astra_network_ethernet_ipv4_validate(candidate) ==
        ASTRA_NETWORK_OK;
}

static void ethernet_countdown(void)
{
    AstraString line;
    uint64_t now = astra_clock_monotonic();
    uint64_t left = ethernet_rollback_at > now ?
        (ethernet_rollback_at - now + UINT64_C(999999999)) /
            UINT64_C(1000000000) : 0u;
    astra_string_init(&line, ethernet_pending, sizeof(ethernet_pending));
    (void)astra_string_append(&line, "Keep new Ethernet settings? Reverts in ");
    (void)astra_string_append_u64(&line, left);
    (void)astra_string_append(&line, " seconds.");
}

static AstraResult ethernet_set_text(uint32_t index, const char *text)
{
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    if (astra_text_model_get_state(&ethernet_models[index], &state) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    return astra_text_model_replace(&ethernet_models[index], 0u,
        state.text_bytes, text, (uint32_t)strlen(text));
}

static int ethernet_address_text(const uint8_t bytes[4], char *text,
                                 uint32_t capacity)
{
    AstraNetworkAddress address = {.size = sizeof(address),
        .family = ASTRA_NETWORK_FAMILY_IPV4};
    (void)memcpy(address.address, bytes, 4u);
    return astra_network_address_format(&address, text, capacity) ==
        ASTRA_NETWORK_OK;
}

static AstraResult ethernet_load_editor(void)
{
    AstraNetworkEthernetIPv4 saved;
    AstraNetworkStatus status;
    char text[80];
    AstraString line;
    if (ethernet_selected == 0u || ethernet_selected > ethernet_count ||
        network_admin_ready == 0u) return ASTRA_OK;
    status = astra_network_ethernet_saved(&network_admin,
        ethernet_names[ethernet_selected - 1u], &saved);
    if (status != ASTRA_NETWORK_OK && status != ASTRA_NETWORK_NAME_NOT_FOUND) {
        (void)astra_log_failure("Settings saved Ethernet query", status);
        return ASTRA_ERROR_IO;
    }
    ethernet_method = status == ASTRA_NETWORK_OK ? saved.method :
        ASTRA_NETWORK_ETHERNET_IPV4_DHCP;
    if (status != ASTRA_NETWORK_OK || ethernet_method ==
            ASTRA_NETWORK_ETHERNET_IPV4_DHCP) {
        for (uint32_t at = 0u; at < 3u; ++at)
            if (ethernet_set_text(at, "") != ASTRA_OK)
                return ASTRA_ERROR_IO;
    } else {
        if (!ethernet_address_text(saved.address, text, sizeof(text)) ||
            ethernet_set_text(0u, text) != ASTRA_OK)
            return ASTRA_ERROR_IO;
        astra_string_init(&line, text, sizeof(text));
        (void)astra_string_append_u64(&line, saved.prefix);
        if (ethernet_set_text(1u, text) != ASTRA_OK)
            return ASTRA_ERROR_IO;
        if (memcmp(saved.gateway, (uint8_t[4]){0}, 4u) == 0)
            text[0] = '\0';
        else if (!ethernet_address_text(saved.gateway, text, sizeof(text)))
            return ASTRA_ERROR_IO;
        if (ethernet_set_text(2u, text) != ASTRA_OK)
            return ASTRA_ERROR_IO;
    }
    astra_string_init(&line, text, sizeof(text));
    if (status == ASTRA_NETWORK_OK)
        for (uint32_t at = 0u; at < saved.dns_count; ++at) {
            char address[20];
            if (!ethernet_address_text(saved.dns[at], address,
                                       sizeof(address)))
                return ASTRA_ERROR_IO;
            if (at != 0u) (void)astra_string_append(&line, ", ");
            (void)astra_string_append(&line, address);
        }
    return ethernet_set_text(3u, text);
}

static AstraResult init_controls(void)
{
    static const AstraToolbarAction actions[] = {
        {"Move Up", 7u, 0u}, {"Move Down", 9u, 0u},
        {"Remove from Dock", 16u, 0u}
    };
    static const char *const titles[] = {
        "", "Dock", "Fonts", "About Astra", "Ethernet"
    };
    static const char *const descriptions[] = {
        "", "Arrange apps pinned to Dock. Running apps appear automatically.",
        "Installed fonts and system face are managed in Fonts.",
        "System information and project details.",
        "Network interfaces and currently assigned addresses."
    };
    AstraContainerInfo root = ASTRA_CONTAINER_INFO_INIT;
    AstraSettingsSidebarInfo sidebar = ASTRA_SETTINGS_SIDEBAR_INFO_INIT;
    AstraSettingsPaneInfo pane = ASTRA_SETTINGS_PANE_INFO_INIT;
    AstraScrollViewInfo scroll = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraListInfo list = ASTRA_LIST_INFO_INIT;
    AstraToolbarInfo toolbar = ASTRA_TOOLBAR_INFO_INIT;
    AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
    AstraChoiceInfo choice = ASTRA_CHOICE_INFO_INIT;
    AstraFieldInfo field = ASTRA_FIELD_INFO_INIT;
    AstraFormRowInfo form = ASTRA_FORM_ROW_INFO_INIT;
    AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraResult result;

    root.id = SETTINGS_ROOT;
    root.layout.direction = ASTRA_FLEX_ROW;
    root.layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    root.layout.main_gap = 20u;
    root.layout.padding_left = root.layout.padding_right = 16u;
    root.layout.padding_top = root.layout.padding_bottom = 16u;
    result = astra_interface_container_init(CONTROL(SETTINGS_ROOT), &root);
    if (result != ASTRA_OK) return result;
    item.grow = 1u;
    item.minimum_width = SETTINGS_WIDTH;
    item.minimum_height = SETTINGS_HEIGHT;
    result = astra_interface_control_set_flex(CONTROL(SETTINGS_ROOT), &item);
    if (result != ASTRA_OK) return result;

    sidebar.first_id = SETTINGS_SIDEBAR;
    sidebar.search = &search_model;
    sidebar.scroll = &sidebar_scroll;
    sidebar.collection = &navigation;
    sidebar.width = SETTINGS_SIDEBAR_WIDTH;
    sidebar.height = SETTINGS_HEIGHT - 32u;
    result = astra_interface_settings_sidebar_init(CONTROL(SETTINGS_SIDEBAR),
                                                   &sidebar);
    if (result != ASTRA_OK) {
        (void)astra_log_failure("Settings sidebar", (uint32_t)-result);
        return result;
    }
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = SETTINGS_ROOT;
    item.minimum_width = item.maximum_width = SETTINGS_SIDEBAR_WIDTH;
    item.minimum_height = SETTINGS_HEIGHT - 32u;
    result = astra_interface_control_set_flex(CONTROL(SETTINGS_SIDEBAR),
                                               &item);
    if (result != ASTRA_OK) return result;

    root = (AstraContainerInfo)ASTRA_CONTAINER_INFO_INIT;
    root.id = SETTINGS_CONTENT;
    root.layout.direction = ASTRA_FLEX_COLUMN;
    root.layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    root.layout.padding_left = 14u;
    root.layout.padding_right = 4u;
    root.layout.padding_top = 12u;
    root.layout.main_gap = 12u;
    result = astra_interface_container_init(CONTROL(SETTINGS_CONTENT), &root);
    if (result != ASTRA_OK) return result;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = SETTINGS_ROOT;
    item.grow = 1u;
    item.minimum_width = 560u;
    item.minimum_height = SETTINGS_HEIGHT - 32u;
    result = astra_interface_control_set_flex(CONTROL(SETTINGS_CONTENT),
                                               &item);
    if (result != ASTRA_OK) return result;

    pane.first_id = SETTINGS_TITLE;
    pane.title = titles[page];
    pane.title_length = (uint32_t)strlen(pane.title);
    pane.description = descriptions[page];
    pane.description_length = (uint32_t)strlen(pane.description);
    result = astra_interface_settings_pane_init(CONTROL(SETTINGS_TITLE), &pane);
    if (result != ASTRA_OK) {
        (void)astra_log_failure("Settings pane", (uint32_t)-result);
        return result;
    }
    for (uint32_t id = SETTINGS_TITLE; id <= SETTINGS_ACTIONS; ++id) {
        item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
        item.parent_id = SETTINGS_CONTENT;
        item.grow = id == SETTINGS_BODY;
        item.align_self = ASTRA_FLEX_ALIGN_STRETCH;
        result = astra_interface_control_set_flex(CONTROL(id), &item);
        if (result != ASTRA_OK) return result;
    }

    scroll.id = SETTINGS_PINS_SCROLL;
    scroll.model = &pins_scroll;
    scroll.preferred_width = 500u;
    scroll.preferred_height = 350u;
    scroll.state = page == SETTINGS_DOCK && pin_count != 0u ?
        0u : ASTRA_CONTROL_COLLAPSED;
    result = astra_interface_scroll_view_init(CONTROL(SETTINGS_PINS_SCROLL),
                                              &scroll);
    if (result != ASTRA_OK) return result;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = SETTINGS_BODY;
    item.grow = 1u;
    item.minimum_width = 500u;
    result = astra_interface_control_set_flex(CONTROL(SETTINGS_PINS_SCROLL),
                                               &item);
    if (result != ASTRA_OK) return result;
    list.id = SETTINGS_PINS_LIST;
    list.model = &pin_list;
    result = astra_interface_list_init(CONTROL(SETTINGS_PINS_LIST), &list);
    if (result != ASTRA_OK) return result;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = SETTINGS_PINS_SCROLL;
    item.minimum_width = 500u;
    result = astra_interface_control_set_flex(CONTROL(SETTINGS_PINS_LIST),
                                               &item);
    if (result != ASTRA_OK) return result;

    scroll = (AstraScrollViewInfo)ASTRA_SCROLL_VIEW_INFO_INIT;
    scroll.id = SETTINGS_ETHERNET_SCROLL;
    scroll.model = &ethernet_scroll;
    scroll.preferred_width = 500u;
    scroll.preferred_height = 112u;
    scroll.state = page == SETTINGS_ETHERNET ? 0u : ASTRA_CONTROL_COLLAPSED;
    result = astra_interface_scroll_view_init(
        CONTROL(SETTINGS_ETHERNET_SCROLL), &scroll);
    if (result != ASTRA_OK) return result;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = SETTINGS_BODY;
    item.minimum_width = 500u;
    item.minimum_height = 112u;
    result = astra_interface_control_set_flex(
        CONTROL(SETTINGS_ETHERNET_SCROLL), &item);
    if (result != ASTRA_OK) return result;
    list = (AstraListInfo)ASTRA_LIST_INFO_INIT;
    list.id = SETTINGS_ETHERNET_LIST;
    list.model = &ethernet_list;
    result = astra_interface_list_init(CONTROL(SETTINGS_ETHERNET_LIST), &list);
    if (result != ASTRA_OK) return result;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = SETTINGS_ETHERNET_SCROLL;
    item.minimum_width = 500u;
    result = astra_interface_control_set_flex(CONTROL(SETTINGS_ETHERNET_LIST),
                                               &item);
    if (result != ASTRA_OK) return result;

    label = (AstraLabelInfo)ASTRA_LABEL_INFO_INIT;
    label.id = SETTINGS_ETHERNET_IPV4;
    label.text = ethernet_ipv4;
    label.text_length = (uint32_t)strlen(ethernet_ipv4);
    label.state = page == SETTINGS_ETHERNET ? 0u : ASTRA_CONTROL_COLLAPSED;
    result = astra_interface_label_init(CONTROL(SETTINGS_ETHERNET_IPV4), &label);
    if (result != ASTRA_OK) return result;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = SETTINGS_BODY;
    result = astra_interface_control_set_flex(
        CONTROL(SETTINGS_ETHERNET_IPV4), &item);
    if (result != ASTRA_OK) return result;
    label.id = SETTINGS_ETHERNET_IPV6;
    label.text = ethernet_ipv6;
    label.text_length = (uint32_t)strlen(ethernet_ipv6);
    result = astra_interface_label_init(CONTROL(SETTINGS_ETHERNET_IPV6), &label);
    if (result != ASTRA_OK) return result;
    result = astra_interface_control_set_flex(
        CONTROL(SETTINGS_ETHERNET_IPV6), &item);
    if (result != ASTRA_OK) return result;

    static const AstraChoiceItem modes[] = {
        {"Automatic (DHCP)", 16u, {0u, 0u}},
        {"Manual", 6u, {0u, 0u}}
    };
    choice.id = SETTINGS_ETHERNET_METHOD;
    choice.items = modes;
    choice.item_count = 2u;
    choice.selected = ethernet_method - 1u;
    choice.state = page == SETTINGS_ETHERNET && network_admin_ready != 0u ?
        (ethernet_rollback_at != 0u ? ASTRA_CONTROL_DISABLED : 0u) :
        ASTRA_CONTROL_COLLAPSED;
    result = astra_interface_segmented_init(CONTROL(SETTINGS_ETHERNET_METHOD),
                                            &choice);
    if (result != ASTRA_OK) return result;
    item.parent_id = SETTINGS_BODY;
    result = astra_interface_control_set_flex(
        CONTROL(SETTINGS_ETHERNET_METHOD), &item);
    if (result != ASTRA_OK) return result;

    static const char *const fields[] = {
        "IP address", "Prefix length", "Gateway", "DNS servers"
    };
    for (uint32_t at = 0u; at < 4u; ++at) {
        uint32_t root_id = ethernet_form_id(at);
        form.first_id = root_id;
        form.label = fields[at];
        form.label_length = (uint32_t)strlen(fields[at]);
        form.message = at == 3u ? "Comma-separated; up to three IPv4 servers" : NULL;
        form.message_length = at == 3u ? 41u : 0u;
        result = astra_interface_form_row_init(CONTROL(root_id), &form);
        if (result != ASTRA_OK) return result;
        item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
        item.parent_id = SETTINGS_BODY;
        result = astra_interface_control_set_flex(CONTROL(root_id), &item);
        if (result != ASTRA_OK) return result;
        field = (AstraFieldInfo)ASTRA_FIELD_INFO_INIT;
        field.id = root_id + ASTRA_FORM_ROW_CONTROL_COUNT;
        field.model = &ethernet_models[at];
        field.preferred_columns = at == 3u ? 38u : 18u;
        field.state = ethernet_rollback_at != 0u ? ASTRA_CONTROL_DISABLED : 0u;
        result = astra_interface_field_init(CONTROL(field.id), &field);
        if (result != ASTRA_OK) return result;
        item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
        item.parent_id = root_id + ASTRA_FORM_ROW_VALUE;
        item.grow = 1u;
        result = astra_interface_control_set_flex(CONTROL(field.id), &item);
        if (result != ASTRA_OK) return result;
    }

    label = (AstraLabelInfo)ASTRA_LABEL_INFO_INIT;
    label.id = SETTINGS_ETHERNET_PENDING;
    label.text = ethernet_pending[0] != '\0' ? ethernet_pending : " ";
    label.text_length = ethernet_pending[0] != '\0' ?
        (uint32_t)strlen(ethernet_pending) : 1u;
    label.text_role = ASTRA_TEXT_CLIENT_SECONDARY;
    label.state = page == SETTINGS_ETHERNET &&
        ethernet_rollback_at != 0u ? 0u : ASTRA_CONTROL_COLLAPSED;
    result = astra_interface_label_init(CONTROL(SETTINGS_ETHERNET_PENDING),
                                        &label);
    if (result != ASTRA_OK) return result;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = SETTINGS_ACTIONS;
    result = astra_interface_control_set_flex(
        CONTROL(SETTINGS_ETHERNET_PENDING), &item);
    if (result != ASTRA_OK) return result;

    static const uint32_t ids[] = {
        SETTINGS_ETHERNET_APPLY, SETTINGS_ETHERNET_KEEP,
        SETTINGS_ETHERNET_REVERT};
    static const char *const texts[] = {
        "Apply", "Keep", "Revert"};
    for (uint32_t at = 0u; at < 3u; ++at) {
        button = (AstraButtonInfo)ASTRA_BUTTON_INFO_INIT;
        button.id = ids[at];
        button.text = texts[at];
        button.text_length = (uint32_t)strlen(texts[at]);
        button.variant = at == 1u ? ASTRA_BUTTON_PRIMARY : ASTRA_BUTTON_STANDARD;
        button.state = page == SETTINGS_ETHERNET &&
            network_admin_ready != 0u &&
            ((at == 0u) == (ethernet_rollback_at == 0u)) ?
                0u : ASTRA_CONTROL_COLLAPSED;
        result = astra_interface_button_init(CONTROL(ids[at]), &button);
        if (result != ASTRA_OK) return result;
        item.parent_id = SETTINGS_ACTIONS;
        result = astra_interface_control_set_flex(CONTROL(ids[at]), &item);
        if (result != ASTRA_OK) return result;
    }

    button.id = SETTINGS_ETHERNET_REFRESH;
    button.text = "Refresh";
    button.text_length = 7u;
    button.variant = ASTRA_BUTTON_STANDARD;
    button.state = page == SETTINGS_ETHERNET ? 0u : ASTRA_CONTROL_COLLAPSED;
    result = astra_interface_button_init(CONTROL(SETTINGS_ETHERNET_REFRESH),
                                         &button);
    if (result != ASTRA_OK) return result;
    item.parent_id = SETTINGS_ACTIONS;
    result = astra_interface_control_set_flex(
        CONTROL(SETTINGS_ETHERNET_REFRESH), &item);
    if (result != ASTRA_OK) return result;

    label = (AstraLabelInfo)ASTRA_LABEL_INFO_INIT;
    label.id = SETTINGS_ETHERNET_EMPTY;
    label.text = network_ready != 0u ?
        "No network interface addresses available." :
        "Network service unavailable.";
    label.text_length = (uint32_t)strlen(label.text);
    label.text_role = ASTRA_TEXT_CLIENT_SECONDARY;
    label.state = page == SETTINGS_ETHERNET && ethernet_count == 0u ?
        0u : ASTRA_CONTROL_COLLAPSED;
    result = astra_interface_label_init(CONTROL(SETTINGS_ETHERNET_EMPTY),
                                        &label);
    if (result != ASTRA_OK) return result;
    item.parent_id = SETTINGS_BODY;
    result = astra_interface_control_set_flex(
        CONTROL(SETTINGS_ETHERNET_EMPTY), &item);
    if (result != ASTRA_OK) return result;

    toolbar.first_id = SETTINGS_TOOLS;
    toolbar.actions = actions;
    toolbar.action_count = 3u;
    result = astra_interface_toolbar_init(CONTROL(SETTINGS_TOOLS), &toolbar);
    if (result != ASTRA_OK) {
        (void)astra_log_failure("Settings toolbar", (uint32_t)-result);
        return result;
    }
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = SETTINGS_ACTIONS;
    result = astra_interface_control_set_flex(CONTROL(SETTINGS_TOOLS), &item);
    if (result != ASTRA_OK) return result;

    button.id = SETTINGS_FONTS_BUTTON;
    button.text = "Open Fonts";
    button.text_length = 10u;
    button.variant = ASTRA_BUTTON_PRIMARY;
    button.state = page == SETTINGS_FONTS ? 0u : ASTRA_CONTROL_COLLAPSED;
    result = astra_interface_button_init(CONTROL(SETTINGS_FONTS_BUTTON),
                                         &button);
    if (result != ASTRA_OK) return result;
    item.parent_id = SETTINGS_ACTIONS;
    result = astra_interface_control_set_flex(CONTROL(SETTINGS_FONTS_BUTTON),
                                               &item);
    if (result != ASTRA_OK) return result;

    label.id = SETTINGS_ABOUT;
    label.text = "Astra68 · MC68040 native desktop · FPGA graphics";
    label.text_length = (uint32_t)strlen(label.text);
    label.state = page == SETTINGS_INFO ? 0u : ASTRA_CONTROL_COLLAPSED;
    result = astra_interface_label_init(CONTROL(SETTINGS_ABOUT), &label);
    if (result != ASTRA_OK) return result;
    item.parent_id = SETTINGS_BODY;
    result = astra_interface_control_set_flex(CONTROL(SETTINGS_ABOUT), &item);
    if (result != ASTRA_OK) return result;

    label = (AstraLabelInfo)ASTRA_LABEL_INFO_INIT;
    label.id = SETTINGS_STATUS;
    label.text = status_text[0] != '\0' ? status_text : " ";
    label.text_length = status_text[0] != '\0' ?
        (uint32_t)strlen(status_text) : 1u;
    label.text_role = ASTRA_TEXT_CLIENT_FAULT;
    label.state = status_text[0] != '\0' ? 0u : ASTRA_CONTROL_COLLAPSED;
    result = astra_interface_label_init(CONTROL(SETTINGS_STATUS), &label);
    if (result != ASTRA_OK) return result;
    item.parent_id = SETTINGS_ACTIONS;
    result = astra_interface_control_set_flex(CONTROL(SETTINGS_STATUS), &item);
    if (result != ASTRA_OK) return result;

    label = (AstraLabelInfo)ASTRA_LABEL_INFO_INIT;
    label.id = SETTINGS_EMPTY;
    label.text = "No pinned apps. Right click an app in Dock and choose Keep in Dock.";
    label.text_length = (uint32_t)strlen(label.text);
    label.text_role = ASTRA_TEXT_CLIENT_SECONDARY;
    label.state = page == SETTINGS_DOCK && pin_count == 0u ?
        0u : ASTRA_CONTROL_COLLAPSED;
    result = astra_interface_label_init(CONTROL(SETTINGS_EMPTY), &label);
    if (result != ASTRA_OK) return result;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = SETTINGS_BODY;
    result = astra_interface_control_set_flex(CONTROL(SETTINGS_EMPTY), &item);
    if (result != ASTRA_OK) return result;

    result = astra_interface_ui_init(&ui, controls, SETTINGS_CONTROL_COUNT,
                                     SETTINGS_WIDTH, SETTINGS_HEIGHT);
    if (result != ASTRA_OK) {
        (void)astra_log_failure("Settings UI init", (uint32_t)-result);
        return result;
    }
    if (astra_interface_ui_set_state(&ui, CONTROL(SETTINGS_TOOLS),
            page == SETTINGS_DOCK && pin_count != 0u ?
                0u : ASTRA_CONTROL_COLLAPSED) != ASTRA_OK) {
        (void)astra_log_failure("Settings UI state", ASTRA_STATUS_IO);
        return ASTRA_ERROR_IO;
    }
    for (uint32_t at = 0u; at < 4u; ++at)
        if (astra_interface_ui_set_state(&ui, CONTROL(ethernet_form_id(at)),
                page == SETTINGS_ETHERNET && network_admin_ready != 0u &&
                (at == 3u || ethernet_method ==
                    ASTRA_NETWORK_ETHERNET_IPV4_MANUAL) ?
                    0u : ASTRA_CONTROL_COLLAPSED) != ASTRA_OK)
            return ASTRA_ERROR_IO;
    layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    result = astra_interface_ui_layout(&ui, &layout);
    if (result != ASTRA_OK)
        (void)astra_log_failure("Settings layout", (uint32_t)-result);
    return result;
}

static AstraResult present(void)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraWindowFrame frame = {0u, 0u, SETTINGS_WIDTH, SETTINGS_HEIGHT};
    if (!astra_draw_list_view_init(&surface.view, surface.view.pixels,
            surface.view.byte_size, SETTINGS_WIDTH, SETTINGS_HEIGHT))
        return ASTRA_ERROR_IO;
    astra_surface_clear(&surface.view, astra_surface_rgb565(
        theme.client.red, theme.client.green, theme.client.blue));
    if (astra_interface_ui_render(&ui, &surface.view) != ASTRA_OK ||
        astra_window_present_region(&window, &frame) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    astra_interface_ui_damage_clear(&ui);
    return ASTRA_OK;
}

static AstraResult rebuild(void)
{
    if (refresh_navigation(0u) != ASTRA_OK ||
        init_controls() != ASTRA_OK || present() != ASTRA_OK)
        return ASTRA_ERROR_IO;
    return ASTRA_OK;
}

static AstraResult run(void)
{
    for (;;) {
        uint32_t handle = astra_window_event_wait_handle(&window);
        uint32_t selected = 0u;
        uint64_t deadline = ethernet_rollback_at != 0u ?
            astra_clock_monotonic() + UINT64_C(1000000000) :
            ASTRA_DEADLINE_FOREVER;
        uint32_t waited = astra_wait_multiple(&handle, 1u, deadline,
                                               &selected, NULL);
        if (waited == ASTRA_SYSCALL_TIMED_OUT &&
            ethernet_rollback_at != 0u) {
            if (astra_clock_monotonic() >= ethernet_rollback_at) {
                (void)astra_network_ethernet_revert(&network_admin);
                ethernet_rollback_at = 0u;
                ethernet_pending[0] = '\0';
                (void)strcpy(status_text, "Ethernet change expired; previous settings restored.");
            } else ethernet_countdown();
            if (rebuild() != ASTRA_OK) return ASTRA_ERROR_IO;
            continue;
        }
        if (waited != ASTRA_SYSCALL_OK)
            return ASTRA_ERROR_IO;
        AstraWindowEvent event = {0};
        AstraUIAction action = ASTRA_UI_ACTION_INIT;
        AstraResult result = astra_window_event_try(&window, &event);
        if (result == ASTRA_ERROR_WOULD_BLOCK) continue;
        if (result != ASTRA_OK) return result;
        if (event.type == ASTRA_WINDOW_EVENT_CLOSE_REQUEST) return ASTRA_OK;
        if (event.type == ASTRA_WINDOW_EVENT_STATE) {
            uint32_t active = (event.data.state.flags &
                ASTRA_WINDOW_ACTIVE) != 0u;
            uint32_t activated = active && !window_active;
            window_active = active;
            if (activated && page == SETTINGS_DOCK) {
                uint32_t previous_generation = pin_generation;
                if (refresh_pins() != ASTRA_OK ||
                    (pin_generation != previous_generation &&
                     rebuild() != ASTRA_OK))
                    return ASTRA_ERROR_IO;
                continue;
            }
            if (activated && page == SETTINGS_ETHERNET) {
                refresh_ethernet();
                if (rebuild() != ASTRA_OK) return ASTRA_ERROR_IO;
                continue;
            }
        }
        if (event.type == ASTRA_WINDOW_EVENT_FONT_CHANGED &&
            astra_fonts_use_system(gui, &event) != ASTRA_OK)
            return ASTRA_ERROR_IO;
        if (astra_interface_ui_handle_event(&ui, &event, &action) != ASTRA_OK)
            return ASTRA_ERROR_IO;
        if (action.type == ASTRA_UI_ACTION_TEXT_CHANGED &&
            action.control_id == SETTINGS_SIDEBAR_SEARCH) {
            if (refresh_navigation(1u) != ASTRA_OK) {
                (void)astra_log_failure("Settings search model", ASTRA_STATUS_IO);
                return ASTRA_ERROR_IO;
            }
            if (present() != ASTRA_OK) {
                (void)astra_log_failure("Settings search frame", ASTRA_STATUS_IO);
                return ASTRA_ERROR_IO;
            }
            continue;
        }
        if (action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
            action.control_id == SETTINGS_SIDEBAR_LIST &&
            navigation.selected_id >= SETTINGS_DOCK &&
            navigation.selected_id <= SETTINGS_ETHERNET) {
            page = (uint32_t)navigation.selected_id;
            if (page == SETTINGS_DOCK && refresh_pins() != ASTRA_OK)
                return ASTRA_ERROR_IO;
            if (page == SETTINGS_ETHERNET) {
                refresh_ethernet();
                if (ethernet_load_editor() != ASTRA_OK)
                    (void)strcpy(status_text,
                        "Could not load saved Ethernet settings.");
            }
            if (config_ready && astra_config_set_u64(&config, "pane", 0u,
                    page) != ASTRA_CONFIG_OK)
                (void)strcpy(status_text, "Could not save Settings view.");
            if (rebuild() != ASTRA_OK) return ASTRA_ERROR_IO;
            continue;
        }
        if (action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
            action.control_id == SETTINGS_PINS_LIST && pin_list.selected_id != 0u)
            selected_pin = (uint32_t)pin_list.selected_id - 1u;
        if (action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
            action.control_id == SETTINGS_ETHERNET_LIST &&
            ethernet_list.selected_id != 0u) {
            ethernet_selected = (uint32_t)ethernet_list.selected_id;
            ethernet_detail();
            if (ethernet_load_editor() != ASTRA_OK)
                (void)strcpy(status_text, "Could not load saved Ethernet settings.");
            if (rebuild() != ASTRA_OK) return ASTRA_ERROR_IO;
            continue;
        }
        if (action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
            action.control_id == SETTINGS_ETHERNET_METHOD &&
            ethernet_rollback_at == 0u) {
            int64_t chosen = 0;
            if (astra_interface_control_get_value(
                    CONTROL(SETTINGS_ETHERNET_METHOD), &chosen, NULL) != ASTRA_OK ||
                chosen < 0 || chosen > 1) return ASTRA_ERROR_IO;
            ethernet_method = (uint32_t)chosen + 1u;
            if (rebuild() != ASTRA_OK) return ASTRA_ERROR_IO;
            continue;
        }
        if (action.type == ASTRA_UI_ACTION_ACTIVATE &&
            action.control_id == SETTINGS_ETHERNET_REFRESH) {
            refresh_ethernet();
            if (rebuild() != ASTRA_OK) return ASTRA_ERROR_IO;
            continue;
        }
        if (action.type == ASTRA_UI_ACTION_ACTIVATE &&
            action.control_id == SETTINGS_ETHERNET_APPLY &&
            ethernet_rollback_at == 0u) {
            AstraNetworkEthernetIPv4 candidate;
            if (!ethernet_candidate(&candidate))
                (void)strcpy(status_text,
                    "Check IP address, prefix (1-32), gateway, and DNS.");
            else {
                uint64_t staged_at = astra_clock_monotonic();
                AstraNetworkStatus changed = astra_network_ethernet_stage(
                    &network_admin, &candidate, 120u);
                if (changed == ASTRA_NETWORK_OK) {
                    ethernet_rollback_at = staged_at +
                        UINT64_C(120000000000);
                    ethernet_countdown();
                    status_text[0] = '\0';
                } else (void)strcpy(status_text,
                    "Ethernet apply failed. Check current connection.");
            }
            if (rebuild() != ASTRA_OK) return ASTRA_ERROR_IO;
            continue;
        }
        if (action.type == ASTRA_UI_ACTION_ACTIVATE &&
            ethernet_rollback_at != 0u &&
            (action.control_id == SETTINGS_ETHERNET_KEEP ||
             action.control_id == SETTINGS_ETHERNET_REVERT)) {
            AstraNetworkStatus changed = action.control_id ==
                SETTINGS_ETHERNET_KEEP ?
                astra_network_ethernet_confirm(&network_admin) :
                astra_network_ethernet_revert(&network_admin);
            if (changed == ASTRA_NETWORK_OK) {
                ethernet_rollback_at = 0u;
                ethernet_pending[0] = '\0';
                status_text[0] = '\0';
                refresh_ethernet();
                if (ethernet_load_editor() != ASTRA_OK)
                    (void)strcpy(status_text,
                        "Could not load saved Ethernet settings.");
            } else if (changed == ASTRA_NETWORK_TIMED_OUT) {
                ethernet_rollback_at = 0u;
                ethernet_pending[0] = '\0';
                refresh_ethernet();
                (void)strcpy(status_text,
                    "Ethernet change expired; previous connection restored.");
            } else (void)strcpy(status_text,
                "Ethernet change failed. Check current connection.");
            if (rebuild() != ASTRA_OK) return ASTRA_ERROR_IO;
            continue;
        }
        if (action.type == ASTRA_UI_ACTION_ACTIVATE &&
            action.control_id == SETTINGS_FONTS_BUTTON) {
            uint32_t pid = 0u;
            if (astra_application_launch(launcher, "/apps/Fonts.app",
                    sizeof("/apps/Fonts.app") - 1u, &pid) != ASTRA_OK)
                (void)strcpy(status_text, "Could not open Fonts.");
            else status_text[0] = '\0';
            if (rebuild() != ASTRA_OK) return ASTRA_ERROR_IO;
            continue;
        }
        if (action.type == ASTRA_UI_ACTION_ACTIVATE && page == SETTINGS_DOCK &&
            pin_count != 0u &&
            action.control_id >= SETTINGS_MOVE_UP &&
            action.control_id <= SETTINGS_REMOVE) {
            AstraResult changed = ASTRA_OK;
            uint32_t target = selected_pin;
            if (action.control_id == SETTINGS_MOVE_UP && selected_pin != 0u)
                target = selected_pin - 1u;
            else if (action.control_id == SETTINGS_MOVE_DOWN &&
                     selected_pin + 1u < pin_count)
                target = selected_pin + 1u;
            if (action.control_id == SETTINGS_REMOVE)
                changed = astra_dock_pin_remove(gui, selected_pin,
                                                pin_generation);
            else if (target != selected_pin)
                changed = astra_dock_pin_move(gui, selected_pin, target,
                                              pin_generation);
            if (changed == ASTRA_OK) {
                selected_pin = target;
                status_text[0] = '\0';
            } else (void)strcpy(status_text,
                changed == ASTRA_ERROR_BUSY ?
                    "Dock changed elsewhere. Refreshed list; try again." :
                    "Dock change failed. Original order kept.");
            if (refresh_pins() != ASTRA_OK || rebuild() != ASTRA_OK)
                return ASTRA_ERROR_IO;
            continue;
        }
        AstraControlFrame damage;
        if (astra_interface_ui_damage(&ui, &damage) == ASTRA_OK &&
            present() != ASTRA_OK)
            return ASTRA_ERROR_IO;
    }
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *gui_cap, *launch_cap, *network_cap,
        *admin_cap, *ready;
    uint32_t status = ASTRA_STATUS_OK;
    uint64_t stored = 0u;
    if (!astra_startup_validate(startup) || startup->argc != 1u)
        return ASTRA_STATUS_INVALID;
    gui_cap = astra_startup_capability(startup, ASTRA_CAPABILITY_GUI);
    launch_cap = astra_startup_capability(startup,
        ASTRA_CAPABILITY_APPLICATION_LAUNCH);
    network_cap = astra_startup_capability(startup, ASTRA_CAPABILITY_NETWORK);
    admin_cap = astra_startup_capability(startup, ASTRA_CAPABILITY_NETWORK_ADMIN);
    ready = astra_startup_capability(startup, ASTRA_CAPABILITY_SERVICE_READY);
    if (gui_cap == NULL || launch_cap == NULL || ready == NULL)
        return ASTRA_STATUS_ACCESS;
    gui = gui_cap->handle;
    launcher = launch_cap->handle;
    config_ready = astra_config_open(startup, &config_schema,
        ASTRA_CONFIG_OPEN_READ | ASTRA_CONFIG_OPEN_WRITE,
        &config, NULL) == ASTRA_CONFIG_OK;
    network_ready = network_cap != NULL &&
        astra_network_session_open(network_cap->handle, &network) ==
            ASTRA_NETWORK_OK;
    network_admin_ready = admin_cap != NULL &&
        astra_network_session_open(admin_cap->handle, &network_admin) ==
            ASTRA_NETWORK_OK;
    if (config_ready && astra_config_get_u64(&config, "pane", 0u,
            &stored) == ASTRA_CONFIG_OK &&
        stored >= SETTINGS_DOCK && stored <= SETTINGS_ETHERNET)
        page = (uint32_t)stored;
    if (page == SETTINGS_ETHERNET) refresh_ethernet();
    AstraTextModelInfo search = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraScrollModelInfo scroll = ASTRA_SCROLL_MODEL_INFO_INIT;
    search.content_arena = search_content;
    search.content_arena_bytes = sizeof(search_content);
    search.metadata_arena = search_metadata;
    search.metadata_arena_bytes = sizeof(search_metadata);
    for (uint32_t at = 0u; at < 4u; ++at) {
        AstraTextModelInfo field = ASTRA_TEXT_MODEL_INFO_INIT;
        ethernet_models[at] = (AstraTextModel)ASTRA_TEXT_MODEL_INIT;
        field.content_arena = ethernet_content[at];
        field.content_arena_bytes = sizeof(ethernet_content[at]);
        field.metadata_arena = ethernet_metadata[at];
        field.metadata_arena_bytes = sizeof(ethernet_metadata[at]);
        if (astra_text_model_init(&ethernet_models[at], &field) != ASTRA_OK)
            return ASTRA_STATUS_IO;
    }
    scroll.line_height = 28u;
    const char *failed = NULL;
    if (astra_text_model_init(&search_model, &search) != ASTRA_OK)
        failed = "Settings text model";
    else if (astra_scroll_init(&sidebar_scroll, &scroll) != ASTRA_OK)
        failed = "Settings sidebar scroll";
    else if (astra_scroll_init(&pins_scroll, &scroll) != ASTRA_OK)
        failed = "Settings pins scroll";
    else if (astra_scroll_init(&ethernet_scroll, &scroll) != ASTRA_OK)
        failed = "Settings Ethernet scroll";
    else if (ethernet_load_editor() != ASTRA_OK)
        failed = "Settings Ethernet profile";
    else if (astra_fonts_use_system(gui, NULL) != ASTRA_OK)
        failed = "Settings font";
    else if (refresh_pins() != ASTRA_OK)
        failed = "Settings Dock query";
    else if (refresh_navigation(0u) != ASTRA_OK)
        failed = "Settings navigation";
    else if (init_controls() != ASTRA_OK)
        failed = "Settings controls";
    else if (astra_shared_draw_list_create(&surface, SETTINGS_WIDTH,
            SETTINGS_HEIGHT) != ASTRA_SYSCALL_OK)
        failed = "Settings surface";
    if (failed != NULL) {
        (void)astra_log_failure(failed, ASTRA_STATUS_IO);
        status = ASTRA_STATUS_IO;
    }
    if (status == ASTRA_STATUS_OK) {
        AstraWindowCreateInfo info = ASTRA_WINDOW_CREATE_INFO_INIT;
        info.x = 460u;
        info.y = 145u;
        info.width = SETTINGS_WIDTH;
        info.height = SETTINGS_HEIGHT;
        info.flags = 0u;
        info.content_format = ASTRA_WINDOW_CONTENT_DRAW_LIST;
        info.title = "Settings";
        info.title_length = 8u;
        info.event_mask = ASTRA_WINDOW_SUBSCRIBE_DEFAULT |
            ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON |
            ASTRA_WINDOW_SUBSCRIBE_KEY |
            ASTRA_WINDOW_SUBSCRIBE_TEXT;
        if (astra_window_create(gui, surface.area, &info, &window) != ASTRA_OK) {
            (void)astra_log_failure("Settings window", ASTRA_STATUS_IO);
            status = ASTRA_STATUS_IO;
        } else if (astra_window_set_application_name(&window, "Settings", 8u) !=
                ASTRA_OK) {
            (void)astra_log_failure("Settings app name", ASTRA_STATUS_IO);
            status = ASTRA_STATUS_IO;
        } else if (present() != ASTRA_OK) {
            (void)astra_log_failure("Settings first frame", ASTRA_STATUS_IO);
            status = ASTRA_STATUS_IO;
        }
    }
    (void)astra_service_ready(ready->handle, status, NULL, 0u);
    (void)astra_close(ready->handle);
    if (status == ASTRA_STATUS_OK && run() != ASTRA_OK) {
        (void)astra_log_failure("Settings event loop", ASTRA_STATUS_IO);
        status = ASTRA_STATUS_IO;
    }
    if (window._private_control != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_window_close(&window);
        if (closed != ASTRA_OK && status == ASTRA_STATUS_OK)
            status = ASTRA_STATUS_IO;
    }
    if (surface.area != ASTRA_INVALID_HANDLE)
        (void)astra_shared_surface_close(&surface);
    astra_text_model_dispose(&search_model);
    for (uint32_t at = 0u; at < 4u; ++at)
        astra_text_model_dispose(&ethernet_models[at]);
    if (ethernet_rollback_at != 0u)
        (void)astra_network_ethernet_revert(&network_admin);
    if (network_admin_ready)
        (void)astra_network_session_close(&network_admin);
    if (network_ready) (void)astra_network_session_close(&network);
    if (config_ready) astra_config_close(&config);
    return (int)status;
}
