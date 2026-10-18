#include "ios_network.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "services/network.h"

/* All LVGL access stays on the UI thread. The child only runs the adapter. */
typedef enum { VIEW_HOME, VIEW_WIFI, VIEW_ETHERNET, VIEW_BLUETOOTH, VIEW_HOTSPOT, VIEW_WIFI_INFO, VIEW_AP_INFO } network_view_t;
typedef hachimi_access_point_t access_point_t;
static const lv_font_t * normal_font;
static const lv_font_t * caption_font;
static const lv_font_t * heading_font;
static lv_obj_t * root;
static lv_obj_t * home;
static lv_obj_t * detail;
static lv_obj_t * detail_body;
static lv_obj_t * message;
static lv_obj_t * wifi_value;
static lv_obj_t * ethernet_value;
static lv_obj_t * wifi_toggle;
static lv_obj_t * wifi_power;
static lv_obj_t * detail_toggle;
static lv_obj_t * detail_wifi_value;
static lv_obj_t * home_connection_icons[3];
static lv_obj_t * home_info;
static lv_obj_t * scan_button;
static lv_obj_t * dialog;
static lv_obj_t * password_input;
static lv_obj_t * password_keyboard;
static lv_obj_t * dialog_error;
static lv_obj_t * dialog_join;
static lv_obj_t * dialog_cancel;
static network_view_t view;
static access_point_t networks[24];
static size_t network_count;
static access_point_t selected;
static size_t inspected_index;
static bool wifi_present;
static bool wifi_enabled;
static bool busy;
static bool status_after_job;
static bool scan_when_idle;
static bool bluetooth_present;
static bool hotspot_present;
static char ssid[100];
static char wifi_ip[64] = "-";
static char wifi_mac[32] = "-";
static char wifi_state[40] = "DISCONNECTED";
static char ethernet_details[512];
static char route[256];
static char dns[128];
static char job[24];
static hachimi_network_service_t network_service;
static lv_timer_t * worker_timer;
static lv_timer_t * refresh_timer;

static void render_detail(void);
static void request(const char * operation, const hachimi_wifi_credentials_t * credentials);

static lv_obj_t * label(lv_obj_t * parent, const char * text, int x, int y,
                        int width, int height, bool caption)
{
    lv_obj_t * obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_text_font(obj, caption ? caption_font : normal_font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(caption ? 0x85858b : 0x202124), 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static lv_obj_t * panel(lv_obj_t * parent, int x, int y, int width, int height)
{
    lv_obj_t * obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_bg_color(obj, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 16, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t * button(lv_obj_t * parent, const char * text, int x, int y,
                         int width, lv_event_cb_t callback, void * data)
{
    lv_obj_t * obj = lv_button_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, 32);
    lv_obj_set_style_radius(obj, 10, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xe5e5ea), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_opa(obj, LV_OPA_50, LV_STATE_DISABLED);
    lv_obj_t * text_obj = label(obj, text, 0, 5, width, 24, false);
    lv_obj_set_style_text_align(text_obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(text_obj, lv_color_hex(0x007aff), 0);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, data);
    return obj;
}

static lv_obj_t * glyph(lv_obj_t * parent, const char * text, int x, int y, uint32_t color)
{
    lv_obj_t * obj = label(parent, text, x, y, 22, 22, false);
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    return obj;
}

static void divider(lv_obj_t * parent, int x, int y, int width)
{
    lv_obj_t * obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, 1);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xe5e5ea), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

static void badge(lv_obj_t * parent, const char * symbol, int x, int y, int size, uint32_t color)
{
    lv_obj_t * obj = panel(parent, x, y, size, size);
    lv_obj_set_style_radius(obj, size / 4, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t * icon = glyph(obj, symbol, 0, 0, 0xffffff);
    lv_obj_set_size(icon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(icon);
}

static lv_obj_t * lock_icon(lv_obj_t * parent, int x, int y)
{
    lv_obj_t * icon = panel(parent, x, y, 18, 18);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t * shackle = panel(icon, 5, 1, 8, 10);
    lv_obj_set_style_bg_opa(shackle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(shackle, 4, 0);
    lv_obj_set_style_border_width(shackle, 1, 0);
    lv_obj_set_style_border_color(shackle, lv_color_hex(0x3c3c43), 0);
    lv_obj_clear_flag(shackle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t * body = panel(icon, 3, 7, 12, 9);
    lv_obj_set_style_radius(body, 2, 0);
    lv_obj_set_style_bg_color(body, lv_color_hex(0x3c3c43), 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_CLICKABLE);
    return icon;
}

static lv_obj_t * wifi_signal_icon(lv_obj_t * parent, int x, int y)
{
    lv_obj_t * icon = panel(parent, x, y, 18, 18);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t * symbol = glyph(icon, LV_SYMBOL_WIFI, 0, 0, 0x3c3c43);
    lv_obj_set_size(symbol, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(symbol);
    return icon;
}

static lv_obj_t * info_button(lv_obj_t * parent, int x, int y, lv_event_cb_t callback, void * data)
{
    lv_obj_t * obj = button(parent, "", x, y, 32, callback, data);
    lv_obj_t * ring = panel(obj, 7, 7, 18, 18);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 1, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(0x007aff), 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    /* Draw the dot and stem directly so font bearings cannot offset the icon. */
    lv_obj_t * dot = panel(ring, 7, 3, 2, 2);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0x007aff), 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t * stem = panel(ring, 7, 7, 2, 6);
    lv_obj_set_style_radius(stem, 1, 0);
    lv_obj_set_style_bg_color(stem, lv_color_hex(0x007aff), 0);
    lv_obj_clear_flag(stem, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static void set_disabled(lv_obj_t * obj, bool disabled)
{
    if(!obj) return;
    if(disabled) lv_obj_add_state(obj, LV_STATE_DISABLED);
    else lv_obj_remove_state(obj, LV_STATE_DISABLED);
}

static void show_message(const char * text, bool error)
{
    lv_label_set_text(message, text);
    lv_obj_set_style_text_color(message, lv_color_hex(error ? 0xd14343 : 0x85858b), 0);
    if(dialog_error) lv_label_set_text(dialog_error, text);
}

static void update_home(void)
{
    lv_label_set_text(wifi_value, !wifi_present ? "未检测到无线网卡" : !wifi_enabled ? "已关闭" :
                      ssid[0] ? ssid : "未连接 · 点击选择网络");
    lv_label_set_text(wifi_power, wifi_enabled ? "已开启" : "已关闭");
    for(size_t i = 0; i < 3; ++i) if(home_connection_icons[i]) {
        if(wifi_enabled && ssid[0]) lv_obj_clear_flag(home_connection_icons[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(home_connection_icons[i], LV_OBJ_FLAG_HIDDEN);
    }
    if(detail_wifi_value) lv_label_set_text(detail_wifi_value,
        !wifi_present ? "未检测到无线网卡" : !wifi_enabled ? "Wi-Fi 已关闭" : ssid[0] ? ssid : "选择一个网络…");
    if(wifi_enabled) lv_obj_add_state(wifi_toggle, LV_STATE_CHECKED);
    else lv_obj_remove_state(wifi_toggle, LV_STATE_CHECKED);
    set_disabled(wifi_toggle, busy || !wifi_present);
    set_disabled(home_info, !wifi_enabled || !ssid[0]);
    if(detail_toggle) {
        if(wifi_enabled) lv_obj_add_state(detail_toggle, LV_STATE_CHECKED);
        else lv_obj_remove_state(detail_toggle, LV_STATE_CHECKED);
        set_disabled(detail_toggle, busy || !wifi_present);
    }
    set_disabled(scan_button, busy || !wifi_enabled);
    set_disabled(dialog_join, busy);
    set_disabled(dialog_cancel, busy);
    if(password_input) set_disabled(password_input, busy || strcmp(selected.security, "OPEN") == 0);
}

static void close_dialog(void)
{
    if(!dialog) return;
    if(password_input) lv_textarea_set_text(password_input, "");
    lv_obj_delete(dialog);
    dialog = password_input = password_keyboard = dialog_error = dialog_join = dialog_cancel = NULL;
    memset(&selected, 0, sizeof(selected));
}

static void navigation_cb(lv_event_t * event)
{
    if(dialog) return;
    view = (network_view_t)(uintptr_t)lv_event_get_user_data(event);
    scan_when_idle = view == VIEW_WIFI;
    if(view == VIEW_HOME) {
        lv_obj_add_flag(detail, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(home, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(home, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(detail, LV_OBJ_FLAG_HIDDEN);
        render_detail();
        if(!busy) {
            bool scan = view == VIEW_WIFI && wifi_enabled;
            if(scan) scan_when_idle = false;
            request(scan ? "scan" : "status", NULL);
        }
    }
}

static void scan_cb(lv_event_t * event)
{
    (void)event;
    if(!busy) request("scan", NULL);
}

static void disconnect_cb(lv_event_t * event)
{
    (void)event;
    if(!busy) request("disconnect", NULL);
}

static void reconnect_cb(lv_event_t * event)
{
    (void)event;
    if(!busy) request("reconnect", NULL);
}

static void toggle_cb(lv_event_t * event)
{
    if(busy) return;
    bool on = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
    request(on ? "wifi-on" : "wifi-off", NULL);
}

static void cancel_cb(lv_event_t * event)
{
    (void)event;
    if(!busy) close_dialog();
}

static void password_visibility_cb(lv_event_t * event)
{
    (void)event;
    if(password_input && !busy)
        lv_textarea_set_password_mode(password_input, !lv_textarea_get_password_mode(password_input));
}

static void join_cb(lv_event_t * event)
{
    (void)event;
    if(busy || !dialog) return;
    const char * password = lv_textarea_get_text(password_input);
    size_t length = strlen(password);
    if(strcmp(selected.security, "PSK") == 0 && (length < 8 || length > 64)) {
        lv_label_set_text(dialog_error, "密码需为 8–63 字节或 64 位十六进制密钥");
        return;
    }
    hachimi_wifi_credentials_t credentials = {selected.ssid, password, selected.security};
    request("connect", &credentials);
}

static void keyboard_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) == LV_EVENT_READY) join_cb(event);
    else if(lv_event_get_code(event) == LV_EVENT_CANCEL) cancel_cb(event);
}

static void wifi_summary(lv_obj_t * parent, int y, bool overview)
{
    lv_obj_t * card = panel(parent, overview ? 14 : 0, y, 554, 176);
    badge(card, LV_SYMBOL_WIFI, 16, 12, 32, 0x009cff);
    lv_obj_t * title = label(card, "Wi-Fi", 16, 52, 510, 26, false);
    lv_obj_set_style_text_font(title, heading_font, 0);
    label(card, "连接 Wi-Fi，查看可用网络并管理无线连接。", 16, 80, 520, 20, true);
    divider(card, 16, 106, 538);
    label(card, "Wi-Fi", 32, 115, 340, 24, false);
    lv_obj_t * toggle = lv_switch_create(card);
    lv_obj_set_pos(toggle, 496, 111);
    lv_obj_set_size(toggle, 42, 24);
    lv_obj_set_style_bg_color(toggle, lv_color_hex(0xe5e5ea), LV_PART_MAIN);
    lv_obj_set_style_bg_color(toggle, lv_color_hex(0x34c759), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(toggle, toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);
    divider(card, 32, 140, 522);
    lv_obj_t * current = button(card, "", 0, 141, 554, navigation_cb,
        (void *)(uintptr_t)(overview || !ssid[0] ? VIEW_WIFI : VIEW_WIFI_INFO));
    lv_obj_set_height(current, 35);
    lv_obj_set_style_radius(current, 0, 0);
    lv_obj_t * check = glyph(current, LV_SYMBOL_OK, 13, 10, 0x007aff);
    lv_obj_t * value = label(current, ssid[0] ? ssid : "选择一个网络…", 32, 8, 392, 24, false);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_obj_t * lock = lock_icon(current, 455, 9);
    lv_obj_t * signal = wifi_signal_icon(current, 483, 9);
    if(!ssid[0] || !wifi_enabled) {
        lv_obj_add_flag(check, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lock, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(signal, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t * information = info_button(current, 504, 2, navigation_cb,
                                        (void *)(uintptr_t)VIEW_WIFI_INFO);
    if(overview) {
        home_info = information;
        home_connection_icons[0] = check;
        home_connection_icons[1] = lock;
        home_connection_icons[2] = signal;
        wifi_toggle = toggle;
        wifi_value = value;
        wifi_power = label(card, "", 380, 115, 100, 22, true);
        lv_obj_add_flag(wifi_power, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        detail_toggle = toggle;
        detail_wifi_value = value;
    }
}

static void select_network_cb(lv_event_t * event)
{
    if((busy && strcmp(job, "status") != 0) || dialog) return;
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);
    if(index >= network_count) return;
    selected = networks[index];
    if(strcmp(selected.security, "UNSUPPORTED") == 0) {
        show_message("此网络需要企业认证或不受支持的加密方式", true);
        return;
    }
    dialog = panel(root, 14, 8, 554, 412);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(0xf2f2f7), 0);
    dialog_cancel = button(dialog, "取消", 2, 0, 68, cancel_cb, NULL);
    dialog_join = button(dialog, "加入", 484, 0, 68, join_cb, NULL);
    lv_obj_t * title = label(dialog, "输入密码", 100, 5, 354, 28, false);
    lv_obj_set_style_text_font(title, heading_font, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t * network_name = label(dialog, selected.ssid, 20, 54, 514, 28, false);
    lv_obj_set_style_text_align(network_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t * field = panel(dialog, 0, 96, 554, 46);
    label(field, "密码", 16, 12, 68, 24, false);
    password_input = lv_textarea_create(field);
    lv_obj_remove_style_all(password_input);
    lv_obj_set_pos(password_input, 88, 12);
    lv_obj_set_size(password_input, 380, 28);
    lv_obj_set_style_text_font(password_input, normal_font, 0);
    lv_obj_set_style_text_color(password_input, lv_color_hex(0x1c1c1e), 0);
    lv_obj_set_style_text_color(password_input, lv_color_hex(0xc7c7cc), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(password_input, lv_color_hex(0x007aff), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(password_input, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_textarea_set_one_line(password_input, true);
    lv_textarea_set_max_length(password_input, 64);
    lv_textarea_set_password_mode(password_input, true);
    lv_textarea_set_placeholder_text(password_input, "必填");
    button(field, "显示", 482, 7, 64, password_visibility_cb, NULL);
    dialog_error = label(dialog, strcmp(selected.security, "OPEN") == 0 ?
                         "这是一个开放网络，无需输入密码。" : "请输入此 Wi-Fi 网络的密码。", 16, 154, 520, 48, true);
    password_keyboard = lv_keyboard_create(dialog);
    lv_obj_set_style_align(password_keyboard, LV_ALIGN_TOP_LEFT, 0);
    lv_obj_set_pos(password_keyboard, 4, 236);
    lv_obj_set_size(password_keyboard, 546, 172);
    lv_obj_set_style_bg_color(password_keyboard, lv_color_hex(0xd1d3d9), 0);
    lv_obj_set_style_bg_opa(password_keyboard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(password_keyboard, 10, 0);
    lv_obj_set_style_bg_color(password_keyboard, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_radius(password_keyboard, 5, LV_PART_ITEMS);
    lv_keyboard_set_textarea(password_keyboard, password_input);
    lv_obj_add_event_cb(password_keyboard, keyboard_cb, LV_EVENT_ALL, NULL);
    if(strcmp(selected.security, "OPEN") == 0) {
        set_disabled(password_input, true);
        lv_obj_add_flag(password_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    update_home();
}

static void inspect_network_cb(lv_event_t * event)
{
    if(dialog || (busy && strcmp(job, "status") != 0)) return;
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);
    if(index >= network_count) return;
    inspected_index = index;
    view = VIEW_AP_INFO;
    scan_when_idle = false;
    render_detail();
}

static void render_detail(void)
{
    if(!detail || view == VIEW_HOME) return;
    detail_toggle = detail_wifi_value = NULL;
    lv_obj_clean(detail);
    scan_button = NULL;
    bool information_page = view == VIEW_WIFI_INFO || view == VIEW_AP_INFO;
    lv_obj_t * back = button(detail, information_page ? "Wi-Fi" : "网络与连接",
                             10, 0, 112, navigation_cb,
                             (void *)(uintptr_t)(information_page ? VIEW_WIFI : VIEW_HOME));
    lv_obj_set_x(lv_obj_get_child(back, 0), 16);
    lv_obj_set_width(lv_obj_get_child(back, 0), 96);
    glyph(back, LV_SYMBOL_LEFT, 0, 9, 0x007aff);
    static const char * titles[] = {"", "Wi-Fi", "以太网", "蓝牙", "个人热点", "网络详情", "网络信息"};
    lv_obj_t * title = label(detail, titles[view], 150, 4, 254, 28, false);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(title, heading_font, 0);
    if(view == VIEW_WIFI) scan_button = button(detail, "刷新", 486, 0, 68, scan_cb, NULL);
    detail_body = panel(detail, 0, 46, 554, 328);
    lv_obj_set_style_bg_opa(detail_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(detail_body, 0, 0);
    if(view == VIEW_WIFI) {
        lv_obj_add_flag(detail_body, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(detail_body, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(detail_body, LV_SCROLLBAR_MODE_AUTO);
        wifi_summary(detail_body, 0, false);
        label(detail_body, "其他网络", 16, 192, 510, 18, true);
        size_t rows = 0;
        if(wifi_enabled) for(size_t i = 0; i < network_count; ++i)
            if(strcmp(networks[i].ssid, ssid) != 0) ++rows;
        lv_obj_t * list = panel(detail_body, 0, 218, 554, rows ? (int)rows * 44 : 44);
        if(!rows) label(list, !wifi_enabled ? "开启 Wi-Fi 后查看可用网络" : busy ?
                       "正在查找网络…" : "暂未发现其他网络，轻点右上角刷新", 16, 12, 520, 24, true);
        size_t row_number = 0;
        if(wifi_enabled) for(size_t i = 0; i < network_count; ++i) {
            if(strcmp(networks[i].ssid, ssid) == 0) continue;
            int y = (int)row_number++ * 44;
            lv_obj_t * row = button(list, networks[i].ssid, 0, y, 554, select_network_cb, (void *)(uintptr_t)i);
            lv_obj_set_height(row, 44);
            lv_obj_set_style_radius(row, 0, 0);
            lv_obj_t * title = lv_obj_get_child(row, 0);
            lv_obj_set_pos(title, 32, 12);
            lv_obj_set_width(title, 380);
            lv_obj_set_style_text_color(title, lv_color_hex(0x1c1c1e), 0);
            lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, 0);
            lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
            if(strcmp(networks[i].security, "OPEN") != 0) lock_icon(row, 455, 13);
            lv_obj_t * signal = wifi_signal_icon(row, 483, 13);
            if(networks[i].signal < -72) lv_obj_set_style_opa(signal, LV_OPA_60, 0);
            info_button(row, 504, 6, inspect_network_cb, (void *)(uintptr_t)i);
            if(row_number < rows) divider(list, 32, y + 43, 522);
        }
        label(detail_body, "选择网络以加入。已保存的网络会自动连接。", 16,
              224 + (int)(rows ? rows : 1) * 44, 520, 18, true);
        if(!ssid[0] && wifi_enabled) button(detail_body, "重新连接已保存的网络", 0,
              258 + (int)(rows ? rows : 1) * 44, 554, reconnect_cb, NULL);
    }
    else if(view == VIEW_AP_INFO) {
        const access_point_t * ap = &networks[inspected_index];
        char strength[40];
        snprintf(strength, sizeof(strength), "%s (%d dBm)", ap->signal > -55 ? "强" : ap->signal > -72 ? "中" : "弱", ap->signal);
        const char * names[] = {"网络名称", "安全性", "信号"};
        const char * values[] = {ap->ssid, strcmp(ap->security, "OPEN") == 0 ? "开放网络" :
            strcmp(ap->security, "PSK") == 0 ? "密码保护" : "企业或其他认证", strength};
        lv_obj_t * card = panel(detail_body, 0, 0, 554, 132);
        for(int i = 0; i < 3; ++i) {
            label(card, names[i], 16, 12 + i * 44, 180, 24, false);
            lv_obj_t * value = label(card, values[i], 208, 12 + i * 44, 328, 24, true);
            lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
            if(i < 2) divider(card, 16, (i + 1) * 44, 538);
        }
        lv_obj_t * action = panel(detail_body, 0, 148, 554, 44);
        button(action, "加入此网络", 0, 6, 554, select_network_cb, (void *)(uintptr_t)inspected_index);
    }
    else if(view == VIEW_WIFI_INFO) {
        lv_obj_t * card = panel(detail_body, 0, 0, 554, 132);
        const char * names[] = {"网络名称", "IP 地址", "Wi-Fi 地址"};
        const char * values[] = {ssid[0] ? ssid : "未连接", wifi_ip, wifi_mac};
        for(int i = 0; i < 3; ++i) {
            label(card, names[i], 16, 12 + i * 44, 180, 24, false);
            lv_obj_t * value = label(card, values[i], 208, 12 + i * 44, 328, 24, true);
            lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
            if(i < 2) divider(card, 16, (i + 1) * 44, 538);
        }
        label(detail_body, "DNS 与路由", 16, 146, 520, 22, true);
        lv_obj_t * dns_card = panel(detail_body, 0, 172, 554, 94);
        label(dns_card, dns[0] ? dns : "未设置 DNS", 16, 12, 520, 24, false);
        divider(dns_card, 16, 44, 538);
        label(dns_card, route[0] ? route : "未设置默认路由", 16, 56, 520, 32, true);
        lv_obj_t * action = panel(detail_body, 0, 282, 554, 44);
        button(action, "断开此网络", 0, 6, 554, disconnect_cb, NULL);
    }
    else if(view == VIEW_ETHERNET) {
        lv_obj_set_style_bg_opa(detail_body, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(detail_body, 16, 0);
        label(detail_body, ethernet_details[0] ? ethernet_details : "未检测到以太网接口", 16, 14, 520, 150, false);
        label(detail_body, "默认路由", 16, 168, 520, 22, true);
        label(detail_body, route[0] ? route : "未设置", 16, 194, 520, 52, false);
        label(detail_body, "DNS 服务器", 16, 252, 520, 22, true);
        label(detail_body, dns[0] ? dns : "未设置", 16, 278, 520, 28, false);
    }
    else if(view == VIEW_BLUETOOTH) {
        lv_obj_set_style_bg_opa(detail_body, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(detail_body, 16, 0);
        label(detail_body, bluetooth_present ? "蓝牙服务可用" : "未检测到可用的蓝牙适配器", 18, 22, 510, 30, false);
        label(detail_body, "此设备尚未启用蓝牙连接服务。\n需要系统提供蓝牙适配器和 BlueZ 服务后才能配对设备。", 18, 72, 510, 90, true);
    }
    else {
        lv_obj_set_style_bg_opa(detail_body, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(detail_body, 16, 0);
        label(detail_body, hotspot_present ? "热点服务尚未配置" : "此设备尚未安装热点服务", 18, 22, 510, 30, false);
        label(detail_body, "个人热点需要无线网卡支持 AP 模式，以及系统热点服务。\n当前 Wi-Fi 用于连接路由器。", 18, 72, 510, 90, true);
    }
    update_home();
}

static bool apply_network_state(void)
{
    const hachimi_network_state_t * state = &network_service.state;
    wifi_present = state->wifi_present;
    wifi_enabled = state->wifi_enabled;
    bluetooth_present = state->bluetooth_present;
    hotspot_present = state->hotspot_present;
    memcpy(ssid, state->ssid, sizeof(ssid));
    memcpy(wifi_ip, state->wifi_ip, sizeof(wifi_ip));
    memcpy(wifi_mac, state->wifi_mac, sizeof(wifi_mac));
    memcpy(wifi_state, state->wifi_state, sizeof(wifi_state));
    memcpy(ethernet_details, state->ethernet_details, sizeof(ethernet_details));
    memcpy(route, state->route, sizeof(route));
    memcpy(dns, state->dns, sizeof(dns));
    memcpy(networks, state->networks, sizeof(networks));
    network_count = state->network_count;
    lv_label_set_text(ethernet_value, state->ethernet_ip);
    if(network_service.message[0]) show_message(network_service.message, network_service.failed);
    return network_service.failed;
}

static void poll_worker(lv_timer_t * timer)
{
    (void)timer;
    if(!busy || !hachimi_network_poll(&network_service)) return;
    busy = false;
    char old_ssid[sizeof(ssid)], old_ip[sizeof(wifi_ip)], old_mac[sizeof(wifi_mac)];
    memcpy(old_ssid, ssid, sizeof(ssid));
    memcpy(old_ip, wifi_ip, sizeof(wifi_ip));
    memcpy(old_mac, wifi_mac, sizeof(wifi_mac));
    bool old_enabled = wifi_enabled;
    bool failed = apply_network_state();
    bool wifi_changed = strcmp(old_ssid, ssid) != 0 || strcmp(old_ip, wifi_ip) != 0 ||
                        strcmp(old_mac, wifi_mac) != 0 || old_enabled != wifi_enabled;
    if(failed && !network_service.message[0])
        show_message("网络服务不可用，请安装 Hachimi 网络适配服务", true);
    if(strcmp(job, "connect") == 0 && !failed) close_dialog();
    if(!failed && strcmp(job, "status") == 0 &&
       strcmp(lv_label_get_text(message), "正在读取设备网络状态…") == 0)
        show_message("", false);
    update_home();
    if(view != VIEW_HOME && !dialog &&
       (view != VIEW_WIFI || strcmp(job, "status") != 0 || wifi_changed)) render_detail();
    if(status_after_job) {
        status_after_job = false;
        request("status", NULL);
    }
    else if(scan_when_idle && view == VIEW_WIFI && !dialog) {
        scan_when_idle = false;
        if(wifi_enabled) request("scan", NULL);
    }
}

static void request(const char * operation, const hachimi_wifi_credentials_t * credentials)
{
    if(busy) return;
    if(!hachimi_system_services_supported()) {
        show_message("电脑预览模式 · 请在开发板上管理网络连接", false);
        return;
    }
    if(!hachimi_network_request(&network_service, operation, credentials)) {
        show_message("无法启动网络操作", true);
        return;
    }
    snprintf(job, sizeof(job), "%s", operation);
    busy = true;
    status_after_job = strcmp(operation, "status") != 0 && strcmp(operation, "scan") != 0;
    if(strcmp(operation, "status") != 0) show_message(strcmp(operation, "scan") == 0 ?
        "正在扫描附近网络…" : strcmp(operation, "connect") == 0 ? "正在连接，请稍候…" : "正在更新网络设置…", false);
    update_home();
}

static void periodic_refresh(lv_timer_t * timer)
{
    (void)timer;
    if(root && !busy && !dialog && lv_obj_is_visible(root))
        request("status", NULL);
}

void ios_network_refresh(void)
{
    if(root && !busy && !dialog) request("status", NULL);
}

static void network_deleted(lv_event_t * event)
{
    (void)event;
    hachimi_network_cancel(&network_service);
    if(worker_timer) lv_timer_delete(worker_timer);
    if(refresh_timer) lv_timer_delete(refresh_timer);
    worker_timer = refresh_timer = NULL;
    root = dialog = NULL;
    busy = false;
}

void ios_network_create(lv_obj_t * parent, const lv_font_t * font, const lv_font_t * small_font,
                        const lv_font_t * title_font)
{
    hachimi_network_init(&network_service);
    busy = status_after_job = scan_when_idle = false;
    dialog = NULL;
    view = VIEW_HOME;

    normal_font = font;
    caption_font = small_font;
    heading_font = title_font;
    root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 582, 432);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    home = lv_obj_create(root);
    lv_obj_remove_style_all(home);
    lv_obj_set_size(home, 582, 392);
    lv_obj_clear_flag(home, LV_OBJ_FLAG_SCROLLABLE);
    wifi_summary(home, 50, true);
    label(home, "其他连接", 30, 242, 520, 18, true);
    lv_obj_t * other = panel(home, 14, 268, 554, 120);
    const char * titles[] = {"蓝牙", "以太网", "个人热点"};
    const char * icons[] = {LV_SYMBOL_BLUETOOTH, LV_SYMBOL_USB, LV_SYMBOL_UPLOAD};
    const network_view_t targets[] = {VIEW_BLUETOOTH, VIEW_ETHERNET, VIEW_HOTSPOT};
    for(size_t i = 0; i < 3; ++i) {
        lv_obj_t * row = button(other, "", 0, (int)i * 40, 554, navigation_cb, (void *)(uintptr_t)targets[i]);
        lv_obj_set_height(row, 40);
        lv_obj_set_style_radius(row, 0, 0);
        badge(row, icons[i], 16, 9, 22, i == 0 ? 0x009cff : 0x34c759);
        label(row, titles[i], 48, 10, 180, 24, false);
        lv_obj_t * value = label(row, i == 1 ? "正在读取…" : "不可用", 228, 10, 280, 24, true);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
        glyph(row, LV_SYMBOL_RIGHT, 524, 14, 0xc7c7cc);
        if(i < 2) divider(other, 48, ((int)i + 1) * 40, 506);
        if(i == 1) ethernet_value = value;
    }
    detail = lv_obj_create(root);
    lv_obj_remove_style_all(detail);
    lv_obj_set_pos(detail, 14, 8);
    lv_obj_set_size(detail, 554, 374);
    lv_obj_set_style_bg_color(detail, lv_color_hex(0xf2f2f7), 0);
    lv_obj_set_style_bg_opa(detail, LV_OPA_COVER, 0);
    lv_obj_clear_flag(detail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(detail, LV_OBJ_FLAG_HIDDEN);
    message = label(root, "正在读取设备网络状态…", 26, 396, 528, 32, true);
    lv_obj_add_event_cb(root, network_deleted, LV_EVENT_DELETE, NULL);
    worker_timer = lv_timer_create(poll_worker, 100, NULL);
    refresh_timer = lv_timer_create(periodic_refresh, 5000, NULL);
    update_home();
    ios_network_refresh();
}
