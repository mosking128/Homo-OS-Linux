#include "ios_time.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "services/time.h"
#include "services/clock.h"

#define IOS_ASSET(name) "A:assets/ios/" name

static lv_obj_t * time_root;
static lv_obj_t * clock_label;
static lv_obj_t * date_label;
static lv_obj_t * timezone_dropdown;
static lv_obj_t * ntp_switch;
static lv_obj_t * sync_label;
static const lv_font_t * time_font;
static const lv_font_t * time_small_font;
static const lv_font_t * time_title_font;
static hachimi_time_service_t time_service;
static char * timezone_options = time_service.zones;
static lv_timer_t * worker_timer;
static lv_timer_t * refresh_timer;
static char current_timezone[128];
static bool busy;
static bool available;
static bool timezone_list_loaded;
static bool updating_ui;
static char job[24];


static lv_obj_t * label(lv_obj_t * parent, const char * text,
                        lv_coord_t x, lv_coord_t y, lv_coord_t width, lv_coord_t height,
                        lv_color_t color, const lv_font_t * font)
{
    lv_obj_t * obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, color, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t * card(lv_obj_t * parent, lv_coord_t x, lv_coord_t y,
                       lv_coord_t width, lv_coord_t height)
{
    lv_obj_t * obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_radius(obj, 15, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static void set_controls_enabled(bool enabled)
{
    if(enabled) {
        lv_obj_remove_state(ntp_switch, LV_STATE_DISABLED);
        if(timezone_list_loaded) lv_obj_remove_state(timezone_dropdown, LV_STATE_DISABLED);
    }
    else {
        lv_obj_add_state(ntp_switch, LV_STATE_DISABLED);
        lv_obj_add_state(timezone_dropdown, LV_STATE_DISABLED);
    }
}

static void update_clock_preview(void)
{
    char clock_text[16], date_text[32];
    if(hachimi_clock_format(clock_text, sizeof(clock_text), "%H:%M:%S")) lv_label_set_text(clock_label, clock_text);
    if(hachimi_clock_format(date_text, sizeof(date_text), "%Y-%m-%d")) lv_label_set_text(date_label, date_text);
}

static bool parse_status(void)
{
    const hachimi_time_state_t * state = &time_service.state;
    char date_text[48];
    lv_snprintf(date_text, sizeof(date_text), "%04d年%02d月%02d日", state->year, state->month, state->day);
    lv_label_set_text(clock_label, state->clock);
    lv_label_set_text(date_label, date_text);
    lv_snprintf(current_timezone, sizeof(current_timezone), "%s", state->timezone);

    updating_ui = true;
    if(state->ntp_enabled) lv_obj_add_state(ntp_switch, LV_STATE_CHECKED);
    else lv_obj_remove_state(ntp_switch, LV_STATE_CHECKED);
    if(timezone_list_loaded) {
        uint16_t index = 0;
        char * line = timezone_options;
        while(*line) {
            char * end = strchr(line, '\n');
            size_t length = end ? (size_t)(end - line) : strlen(line);
            if(strlen(current_timezone) == length && strncmp(line, current_timezone, length) == 0) {
                lv_dropdown_set_selected(timezone_dropdown, index);
                break;
            }
            if(!end) break;
            line = end + 1;
            index++;
        }
    }
    updating_ui = false;

    available = true;
    set_controls_enabled(!busy);
    if(!state->ntp_enabled) {
        lv_label_set_text(sync_label, "联网校对已关闭");
    }
    else if(state->synchronized) {
        lv_label_set_text(sync_label, "已与网络时间同步");
    }
    else {
        lv_label_set_text(sync_label, "正在等待网络时间同步");
    }
    return true;
}

static bool parse_zones(void)
{
    lv_dropdown_set_options(timezone_dropdown, timezone_options);
    timezone_list_loaded = true;
    updating_ui = true;
    uint16_t index = 0;
    char * zone = timezone_options;
    while(*zone) {
        char * end = strchr(zone, '\n');
        size_t length = end ? (size_t)(end - zone) : strlen(zone);
        if(strlen(current_timezone) == length && strncmp(zone, current_timezone, length) == 0) {
            lv_dropdown_set_selected(timezone_dropdown, index);
            break;
        }
        if(!end) break;
        zone = end + 1;
        index++;
    }
    updating_ui = false;
    lv_obj_clear_state(timezone_dropdown, LV_STATE_DISABLED);
    return true;
}

static void request(const char * operation, const char * payload);

static void worker_timer_cb(lv_timer_t * timer)
{
    (void)timer;
    if(!busy || !hachimi_time_poll(&time_service)) return;
    busy = false;
    bool succeeded = time_service.succeeded;
    if(strcmp(job, "status") == 0) {
        if(!succeeded || !parse_status()) {
            available = false;
            set_controls_enabled(false);
            lv_label_set_text(sync_label, "无法读取系统时间服务");
            return;
        }
        if(!timezone_list_loaded) request("zones", NULL);
    }
    else if(strcmp(job, "zones") == 0) {
        if(!succeeded || !parse_zones()) {
            available = false;
            lv_label_set_text(sync_label, "无法读取系统时区列表");
            set_controls_enabled(false);
            return;
        }
        set_controls_enabled(available);
    }
    else if(strcmp(job, "ntp-on") == 0 || strcmp(job, "ntp-off") == 0 ||
            strcmp(job, "zone-set") == 0) {
        if(!succeeded) lv_label_set_text(sync_label, "系统设置更新失败");
        request("status", NULL);
    }
}

static void request(const char * operation, const char * payload)
{
    if(busy) return;
    if(!hachimi_system_services_supported()) {
        lv_label_set_text(sync_label, "电脑预览模式 · 时间设置在开发板上生效");
        return;
    }
    if(!hachimi_time_request(&time_service, operation, payload)) {
        lv_label_set_text(sync_label, "无法启动系统时间服务");
        return;
    }
    snprintf(job, sizeof(job), "%s", operation);
    busy = true;
    set_controls_enabled(false);
}

static void ntp_changed_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED || updating_ui || busy) return;
    bool enabled = lv_obj_has_state(ntp_switch, LV_STATE_CHECKED);
    request(enabled ? "ntp-on" : "ntp-off", NULL);
}

static void timezone_changed_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED || updating_ui || busy ||
       !timezone_list_loaded) return;
    char timezone[128];
    lv_dropdown_get_selected_str(timezone_dropdown, timezone, sizeof(timezone));
    if(timezone[0] && strcmp(timezone, current_timezone) != 0)
        request("zone-set", timezone);
}

static void refresh_timer_cb(lv_timer_t * timer)
{
    (void)timer;
    if(time_root == NULL || !lv_obj_is_visible(time_root)) return;
    if(!hachimi_system_services_supported()) update_clock_preview();
    else if(!busy) request("status", NULL);
}

void ios_time_refresh(void)
{
    if(time_root == NULL || busy) return;
    if(!hachimi_system_services_supported()) {
        update_clock_preview();
        lv_label_set_text(sync_label, "电脑预览模式 · 时间设置在开发板上生效");
    }
    else request("status", NULL);
}

static void time_deleted(lv_event_t * event)
{
    (void)event;
    hachimi_time_cancel(&time_service);
    if(worker_timer) lv_timer_delete(worker_timer);
    if(refresh_timer) lv_timer_delete(refresh_timer);
    worker_timer = refresh_timer = NULL;
    time_root = NULL;
    busy = false;
}

void ios_time_create(lv_obj_t * parent, const lv_font_t * font,
                     const lv_font_t * small_font, const lv_font_t * heading_font,
                     const lv_font_t * title_font)
{
    hachimi_time_init(&time_service);
    busy = available = timezone_list_loaded = updating_ui = false;
    current_timezone[0] = '\0';

    time_font = font;
    time_small_font = small_font;
    time_title_font = title_font;

    time_root = lv_obj_create(parent);
    lv_obj_remove_style_all(time_root);
    lv_obj_set_size(time_root, 582, 432);
    lv_obj_set_style_bg_color(time_root, lv_color_hex(0xf2f2f7), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(time_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(time_root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * intro = card(time_root, 14, 50, 554, 112);
    lv_obj_t * icon = lv_image_create(intro);
    lv_image_set_src(icon, IOS_ASSET("settings_time.png"));
    lv_obj_set_pos(icon, 14, 12);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    label(intro, "时间与日期", 14, 56, 500, 30,
          lv_color_hex(0x17181b), time_title_font);
    label(intro, "设置本地时区，并使用网络时间保持时钟准确。", 14, 88, 520, 20,
          lv_color_hex(0x85858b), time_small_font);

    label(time_root, "日期与时间", 30, 182, 520, 18,
          lv_color_hex(0x85858b), time_small_font);
    lv_obj_t * clock_card = card(time_root, 14, 208, 554, 112);
    clock_label = label(clock_card, "--:--:--", 18, 10, 220, 34,
                        lv_color_hex(0x202124), time_title_font);
    date_label = label(clock_card, "正在读取日期…", 18, 46, 300, 20,
                       lv_color_hex(0x85858b), time_small_font);
    lv_obj_t * divider = lv_obj_create(clock_card);
    lv_obj_remove_style_all(divider);
    lv_obj_set_pos(divider, 18, 74);
    lv_obj_set_size(divider, 518, 1);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0xe5e5ea), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    label(clock_card, "时区", 18, 82, 100, 22,
          lv_color_hex(0x202124), time_font);

    timezone_dropdown = lv_dropdown_create(time_root);
    lv_dropdown_set_options(timezone_dropdown, "正在读取…");
    lv_dropdown_set_dir(timezone_dropdown, LV_DIR_TOP);
    lv_dropdown_set_symbol(timezone_dropdown, NULL);
    lv_obj_remove_style_all(timezone_dropdown);
    lv_obj_set_pos(timezone_dropdown, 150, 287);
    lv_obj_set_size(timezone_dropdown, 404, 30);
    lv_obj_set_style_bg_opa(timezone_dropdown, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_right(timezone_dropdown, 18, LV_PART_MAIN);
    lv_obj_set_style_text_font(timezone_dropdown, time_font, LV_PART_MAIN);
    lv_obj_set_style_text_color(timezone_dropdown, lv_color_hex(0x707078), LV_PART_MAIN);
    lv_obj_set_style_text_align(timezone_dropdown, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    static const lv_point_precise_t timezone_chevron_points[] = {{0, 0}, {4, 4}, {8, 0}};
    lv_obj_t * timezone_chevron = lv_line_create(time_root);
    lv_line_set_points(timezone_chevron, timezone_chevron_points, 3);
    lv_obj_set_pos(timezone_chevron, 548, 300);
    lv_obj_set_style_line_width(timezone_chevron, 2, LV_PART_MAIN);
    lv_obj_set_style_line_color(timezone_chevron, lv_color_hex(0xb6b6bb), LV_PART_MAIN);
    lv_obj_clear_flag(timezone_chevron, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t * timezone_list = lv_dropdown_get_list(timezone_dropdown);
    lv_obj_set_style_bg_color(timezone_list, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(timezone_list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_font(timezone_list, time_font, LV_PART_MAIN);
    lv_obj_set_style_text_color(timezone_list, lv_color_hex(0x202124), LV_PART_MAIN);
    lv_obj_set_style_max_height(timezone_list, 340, LV_PART_MAIN);
    lv_obj_add_state(timezone_dropdown, LV_STATE_DISABLED);
    lv_obj_add_event_cb(timezone_dropdown, timezone_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    label(time_root, "自动校对", 30, 332, 520, 18,
          lv_color_hex(0x85858b), time_small_font);
    lv_obj_t * ntp_card = card(time_root, 14, 358, 554, 48);
    label(ntp_card, "联网校对时间", 18, 12, 390, 24,
          lv_color_hex(0x202124), heading_font);
    ntp_switch = lv_switch_create(ntp_card);
    lv_obj_set_pos(ntp_switch, 508, 11);
    lv_obj_set_size(ntp_switch, 38, 25);
    lv_obj_set_style_bg_color(ntp_switch, lv_color_hex(0xe5e5ea), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ntp_switch, lv_color_hex(0x34c759), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_state(ntp_switch, LV_STATE_DISABLED);
    lv_obj_add_event_cb(ntp_switch, ntp_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    sync_label = label(time_root, "正在读取系统时间服务…", 30, 412, 522, 18,
                       lv_color_hex(0x85858b), time_small_font);

    lv_obj_add_event_cb(time_root, time_deleted, LV_EVENT_DELETE, NULL);
    worker_timer = lv_timer_create(worker_timer_cb, 100, NULL);
    refresh_timer = lv_timer_create(refresh_timer_cb, 5000, NULL);
    ios_time_refresh();
}
