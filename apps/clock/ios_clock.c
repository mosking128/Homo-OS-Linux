#include "ios_clock.h"

#include "framework/ui/fonts.h"
#include "framework/ui/ios_status_bar.h"
#include "services/alert.h"
#include "services/filesystem.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CLOCK_ALARM_CAPACITY 8
#define CLOCK_LAP_CAPACITY 8
#define CLOCK_WORLD_CITY_COUNT 5

#define CLOCK_BG 0x000000
#define CLOCK_SIDEBAR 0x1c1c1e
#define CLOCK_CARD 0x242426
#define CLOCK_SEPARATOR 0x38383a
#define CLOCK_SECONDARY 0x8e8e93
#define CLOCK_ORANGE 0xff9f0a
#define CLOCK_GREEN 0x30d158
#define CLOCK_RED 0xff453a
#define CLOCK_BLUE 0x0a84ff

typedef enum {
    CLOCK_PAGE_WORLD,
    CLOCK_PAGE_ALARMS,
    CLOCK_PAGE_STOPWATCH,
    CLOCK_PAGE_TIMER,
    CLOCK_PAGE_COUNT
} clock_page_t;

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t repeat_days; /* tm_wday bit: Sunday is bit 0. */
    bool enabled;
    int last_fired_date;
} clock_alarm_t;

typedef struct {
    const char * name;
    int utc_offset_minutes;
    bool europe_dst;
    bool us_dst;
} world_city_t;

typedef struct {
    uint64_t elapsed_ms;
    uint64_t split_ms;
} stopwatch_lap_t;

static const world_city_t world_cities[CLOCK_WORLD_CITY_COUNT] = {
    {"伦敦", 0, true, false},
    {"巴黎", 60, true, false},
    {"纽约", -300, false, true},
    {"东京", 540, false, false},
    {"北京", 480, false, false},
};

static const char * const weekday_short[] = {"日", "一", "二", "三", "四", "五", "六"};
static const uint8_t weekday_bits_ui[] = {
    1u << 1, 1u << 2, 1u << 3, 1u << 4, 1u << 5, 1u << 6, 1u << 0
};
static const char * const nav_titles[CLOCK_PAGE_COUNT] = {
    "世界时钟", "闹钟", "秒表", "计时器"
};

static lv_obj_t * clock_root;
static lv_obj_t * page_area;
static lv_obj_t * navigation_labels[CLOCK_PAGE_COUNT];
static lv_obj_t * world_time_labels[CLOCK_WORLD_CITY_COUNT + 1];
static lv_obj_t * world_date_labels[CLOCK_WORLD_CITY_COUNT + 1];
static lv_obj_t * stopwatch_label;
static lv_obj_t * stopwatch_lap_list;
static lv_obj_t * timer_value_label;
static lv_obj_t * timer_picker_panel;
static lv_obj_t * timer_rollers[3];
static lv_obj_t * alarm_editor;
static lv_obj_t * editor_hour_roller;
static lv_obj_t * editor_minute_roller;
static lv_obj_t * ring_dialog;
static lv_timer_t * clock_timer;
static lv_timer_t * runtime_timer;
static lv_obj_t * alarm_time_labels[CLOCK_ALARM_CAPACITY];

static clock_alarm_t alarms[CLOCK_ALARM_CAPACITY];
static size_t alarm_count;
static bool alarm_edit_mode;
static bool alarm_storage_loaded;
static char alarm_storage_path[HACHIMI_FS_PATH_MAX];
static clock_page_t current_page = CLOCK_PAGE_WORLD;

static bool stopwatch_running;
static uint32_t stopwatch_started_tick;
static uint64_t stopwatch_accumulated_ms;
static stopwatch_lap_t stopwatch_laps[CLOCK_LAP_CAPACITY];
static size_t stopwatch_lap_count;

static bool timer_running;
static bool timer_paused;
static uint32_t timer_last_tick;
static uint64_t timer_remaining_ms;
static uint32_t timer_selection[3] = {0, 5, 0};
static uint8_t editor_repeat_days;
static int editor_alarm_index = -1;
static time_t snooze_due[CLOCK_ALARM_CAPACITY + 1];
typedef struct {
    char title[40];
    char time_text[20];
    bool can_snooze;
} clock_alert_t;
static clock_alert_t pending_alerts[CLOCK_ALARM_CAPACITY + 2];
static size_t pending_alert_count;

static void render_current_page(void);
static void open_alarm_editor(int index);
static void show_ring_dialog(const char * title, const char * time_text, bool can_snooze);

static lv_color_t color(uint32_t hex)
{
    return lv_color_hex(hex);
}

static const lv_font_t * font(unsigned size)
{
    const lv_font_t * result = hachimi_ui_font(size);
    return result != NULL ? result : &lv_font_montserrat_14;
}

static lv_obj_t * make_label(lv_obj_t * parent, const char * text,
                             lv_coord_t x, lv_coord_t y,
                             lv_coord_t width, lv_coord_t height,
                             uint32_t text_color, unsigned font_size,
                             lv_text_align_t align)
{
    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, font(font_size), LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color(text_color), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return label;
}

static lv_obj_t * make_panel(lv_obj_t * parent, lv_coord_t x, lv_coord_t y,
                             lv_coord_t width, lv_coord_t height,
                             uint32_t background, lv_coord_t radius)
{
    lv_obj_t * panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_radius(panel, radius, LV_PART_MAIN);
    lv_obj_set_style_bg_color(panel, color(background), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

static lv_obj_t * make_button(lv_obj_t * parent, const char * text,
                              lv_coord_t x, lv_coord_t y,
                              lv_coord_t width, lv_coord_t height,
                              uint32_t background, uint32_t foreground,
                              unsigned font_size, lv_event_cb_t callback,
                              void * user_data)
{
    lv_obj_t * button = lv_button_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, height / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, color(background), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, color(background), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font(font_size), LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color(foreground), LV_PART_MAIN);
    lv_obj_center(label);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    if(callback != NULL) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    return button;
}

static void make_separator(lv_obj_t * parent, lv_coord_t y, lv_coord_t x, lv_coord_t width)
{
    lv_obj_t * line = lv_obj_create(parent);
    lv_obj_remove_style_all(line);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_size(line, width, 1);
    lv_obj_set_style_bg_color(line, color(CLOCK_SEPARATOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

static void ensure_alarm_storage(void)
{
    if(alarm_storage_path[0] != '\0') return;

    char home[HACHIMI_FS_PATH_MAX];
    char directory[HACHIMI_FS_PATH_MAX];
    if(!hachimi_fs_home(home, sizeof(home)) ||
       !hachimi_fs_join(home, ".hachimi", directory, sizeof(directory))) return;

    if(!hachimi_fs_exists(directory) && !hachimi_fs_mkdir(directory)) return;
    if(!hachimi_fs_is_directory(directory)) return;
    (void)hachimi_fs_join(directory, "clock-alarms.txt",
                          alarm_storage_path, sizeof(alarm_storage_path));
}

static void load_alarms(void)
{
    if(alarm_storage_loaded) return;
    alarm_storage_loaded = true;
    alarm_count = 0;
    ensure_alarm_storage();
    if(alarm_storage_path[0] == '\0') return;

    char contents[2048];
    size_t length = 0;
    if(!hachimi_fs_read_text(alarm_storage_path, contents, sizeof(contents), &length)) return;

    char * line = contents;
    while(*line != '\0' && alarm_count < CLOCK_ALARM_CAPACITY) {
        char * next = strchr(line, '\n');
        if(next != NULL) *next = '\0';
        unsigned hour = 0;
        unsigned minute = 0;
        unsigned repeat_days = 0;
        unsigned enabled = 0;
        int last_fired = 0;
        if(sscanf(line, "%u,%u,%u,%u,%d", &hour, &minute, &repeat_days, &enabled, &last_fired) >= 4 &&
           hour < 24 && minute < 60 && repeat_days <= 127) {
            clock_alarm_t * alarm = &alarms[alarm_count++];
            alarm->hour = (uint8_t)hour;
            alarm->minute = (uint8_t)minute;
            alarm->repeat_days = (uint8_t)repeat_days;
            alarm->enabled = enabled != 0;
            alarm->last_fired_date = last_fired;
        }
        if(next == NULL) break;
        line = next + 1;
    }
    (void)length;
}

static void save_alarms(void)
{
    ensure_alarm_storage();
    if(alarm_storage_path[0] == '\0') return;

    char contents[1024];
    size_t used = 0;
    contents[0] = '\0';
    for(size_t i = 0; i < alarm_count; i++) {
        int written = snprintf(contents + used, sizeof(contents) - used,
                               "%u,%u,%u,%u,%d\n", alarms[i].hour,
                               alarms[i].minute, alarms[i].repeat_days,
                               alarms[i].enabled ? 1u : 0u, alarms[i].last_fired_date);
        if(written < 0 || (size_t)written >= sizeof(contents) - used) return;
        used += (size_t)written;
    }
    (void)hachimi_fs_write_text(alarm_storage_path, contents,
                                 !hachimi_fs_exists(alarm_storage_path));
}

static int date_key(const struct tm * value)
{
    return (value->tm_year + 1900) * 10000 + (value->tm_mon + 1) * 100 + value->tm_mday;
}

static int days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    int era = (year >= 0 ? year : year - 399) / 400;
    unsigned year_of_era = (unsigned)(year - era * 400);
    unsigned adjusted_month = month > 2 ? month - 3 : month + 9;
    unsigned day_of_year = (153 * adjusted_month + 2) / 5 + day - 1;
    unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + (int)day_of_era;
}

static unsigned weekday_for_date(int year, unsigned month, unsigned day)
{
    static const int offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if(month < 3) year--;
    return (unsigned)(year + year / 4 - year / 100 + year / 400 + offsets[month - 1] + (int)day) % 7;
}

static unsigned nth_sunday(int year, unsigned month, unsigned nth)
{
    unsigned first_weekday = weekday_for_date(year, month, 1);
    unsigned first_sunday = 1 + (7 - first_weekday) % 7;
    return first_sunday + 7 * (nth - 1);
}

static unsigned last_sunday(int year, unsigned month, unsigned days_in_month)
{
    return days_in_month - weekday_for_date(year, month, days_in_month);
}

static bool utc_is_europe_summer(const struct tm * utc)
{
    int year = utc->tm_year + 1900;
    unsigned start_day = last_sunday(year, 3, 31);
    unsigned end_day = last_sunday(year, 10, 31);
    bool after_start = utc->tm_mon + 1 > 3 ||
                       (utc->tm_mon + 1 == 3 &&
                        ((unsigned)utc->tm_mday > start_day ||
                         ((unsigned)utc->tm_mday == start_day && utc->tm_hour >= 1)));
    bool before_end = utc->tm_mon + 1 < 10 ||
                      (utc->tm_mon + 1 == 10 &&
                       ((unsigned)utc->tm_mday < end_day ||
                        ((unsigned)utc->tm_mday == end_day && utc->tm_hour < 1)));
    return after_start && before_end;
}

static bool utc_is_us_summer(const struct tm * utc)
{
    int year = utc->tm_year + 1900;
    unsigned start_day = nth_sunday(year, 3, 2);
    unsigned end_day = nth_sunday(year, 11, 1);
    bool after_start = utc->tm_mon + 1 > 3 ||
                       (utc->tm_mon + 1 == 3 &&
                        ((unsigned)utc->tm_mday > start_day ||
                         ((unsigned)utc->tm_mday == start_day && utc->tm_hour >= 7)));
    bool before_end = utc->tm_mon + 1 < 11 ||
                      (utc->tm_mon + 1 == 11 &&
                       ((unsigned)utc->tm_mday < end_day ||
                        ((unsigned)utc->tm_mday == end_day && utc->tm_hour < 6)));
    return after_start && before_end;
}

static void format_clock_time(const struct tm * value, char * output, size_t capacity)
{
    if(value == NULL) {
        snprintf(output, capacity, "--:--");
        return;
    }
    snprintf(output, capacity, "%02d:%02d", value->tm_hour, value->tm_min);
}

static void format_date_and_day_difference(const struct tm * city_time,
                                           const struct tm * local_time,
                                           char * output, size_t capacity)
{
    if(city_time == NULL || local_time == NULL) {
        if(capacity > 0) output[0] = '\0';
        return;
    }
    int difference = days_from_civil(city_time->tm_year + 1900,
                                     (unsigned)city_time->tm_mon + 1,
                                     (unsigned)city_time->tm_mday) -
                     days_from_civil(local_time->tm_year + 1900,
                                     (unsigned)local_time->tm_mon + 1,
                                     (unsigned)local_time->tm_mday);
    const char * relation = difference == 0 ? "今天" : (difference < 0 ? "昨天" : "明天");
    snprintf(output, capacity, "%s · %04d年%02d月%02d日", relation,
             city_time->tm_year + 1900, city_time->tm_mon + 1, city_time->tm_mday);
}

static void refresh_world_clock(void)
{
    if(world_time_labels[0] == NULL) return;
    time_t now = time(NULL);
    struct tm local_value;
    struct tm * local_ptr = localtime(&now);
    if(local_ptr == NULL) return;
    local_value = *local_ptr;

    char time_text[16];
    char date_text[48];
    format_clock_time(&local_value, time_text, sizeof(time_text));
    format_date_and_day_difference(&local_value, &local_value, date_text, sizeof(date_text));
    lv_label_set_text(world_time_labels[0], time_text);
    lv_label_set_text(world_date_labels[0], date_text);

    struct tm utc_value;
    struct tm * utc_ptr = gmtime(&now);
    if(utc_ptr == NULL) return;
    utc_value = *utc_ptr;

    for(size_t i = 0; i < CLOCK_WORLD_CITY_COUNT; i++) {
        int offset = world_cities[i].utc_offset_minutes;
        if(world_cities[i].europe_dst && utc_is_europe_summer(&utc_value)) offset += 60;
        if(world_cities[i].us_dst && utc_is_us_summer(&utc_value)) offset += 60;
        time_t city_epoch = now + (time_t)offset * 60;
        struct tm city_value;
        struct tm * city_ptr = gmtime(&city_epoch);
        if(city_ptr == NULL) continue;
        city_value = *city_ptr;
        format_clock_time(&city_value, time_text, sizeof(time_text));
        format_date_and_day_difference(&city_value, &local_value, date_text, sizeof(date_text));
        lv_label_set_text(world_time_labels[i + 1], time_text);
        lv_label_set_text(world_date_labels[i + 1], date_text);
    }
}

static uint64_t stopwatch_elapsed_now(void)
{
    if(!stopwatch_running) return stopwatch_accumulated_ms;
    return stopwatch_accumulated_ms + (uint64_t)lv_tick_elaps(stopwatch_started_tick);
}

static void format_duration(uint64_t milliseconds, bool centiseconds,
                            char * output, size_t capacity)
{
    uint64_t total_seconds = milliseconds / 1000;
    uint64_t hours = total_seconds / 3600;
    uint64_t minutes = (total_seconds / 60) % 60;
    uint64_t seconds = total_seconds % 60;
    if(centiseconds) {
        uint64_t fraction = (milliseconds % 1000) / 10;
        if(hours > 0) {
            snprintf(output, capacity, "%02llu:%02llu:%02llu.%02llu",
                     (unsigned long long)hours, (unsigned long long)minutes,
                     (unsigned long long)seconds, (unsigned long long)fraction);
        }
        else {
            snprintf(output, capacity, "%02llu:%02llu.%02llu",
                     (unsigned long long)minutes, (unsigned long long)seconds,
                     (unsigned long long)fraction);
        }
    }
    else {
        snprintf(output, capacity, "%02llu:%02llu:%02llu",
                 (unsigned long long)hours, (unsigned long long)minutes,
                 (unsigned long long)seconds);
    }
}

static void update_stopwatch_display(void)
{
    if(stopwatch_label == NULL) return;
    char text[32];
    format_duration(stopwatch_elapsed_now(), true, text, sizeof(text));
    lv_label_set_text(stopwatch_label, text);
}

static void update_timer_display(void)
{
    if(timer_value_label == NULL) return;
    uint64_t value = timer_remaining_ms;
    if(timer_running || timer_paused) value = ((value + 999) / 1000) * 1000;
    if(!timer_running && !timer_paused && timer_rollers[0] != NULL) {
        uint64_t hours = lv_roller_get_selected(timer_rollers[0]);
        uint64_t minutes = lv_roller_get_selected(timer_rollers[1]);
        uint64_t seconds = lv_roller_get_selected(timer_rollers[2]);
        value = (hours * 3600 + minutes * 60 + seconds) * 1000;
    }
    char text[32];
    format_duration(value, false, text, sizeof(text));
    lv_label_set_text(timer_value_label, text);
}

static bool alarm_day_matches(const clock_alarm_t * alarm, const struct tm * local)
{
    return alarm->repeat_days == 0 ||
           (alarm->repeat_days & (uint8_t)(1u << local->tm_wday)) != 0;
}

static void close_ring_dialog(void)
{
    hachimi_alert_stop();
    if(ring_dialog != NULL) {
        lv_obj_delete(ring_dialog);
        ring_dialog = NULL;
    }
}

static void ring_dialog_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    uintptr_t action = (uintptr_t)lv_event_get_user_data(event);
    if(action == 1 && ring_dialog != NULL) {
        for(size_t i = 0; i < CLOCK_ALARM_CAPACITY + 1; i++) {
            if(snooze_due[i] == 0) { snooze_due[i] = time(NULL) + 9 * 60; break; }
        }
    }
    close_ring_dialog();
    if(pending_alert_count > 0) {
        clock_alert_t next = pending_alerts[0];
        pending_alert_count--;
        memmove(pending_alerts, pending_alerts + 1, pending_alert_count * sizeof(pending_alerts[0]));
        show_ring_dialog(next.title, next.time_text, next.can_snooze);
    }
}

static void show_ring_dialog(const char * title, const char * time_text, bool can_snooze)
{
    if(ring_dialog != NULL) {
        if(pending_alert_count < CLOCK_ALARM_CAPACITY + 2) {
            clock_alert_t * pending = &pending_alerts[pending_alert_count++];
            snprintf(pending->title, sizeof(pending->title), "%s", title);
            snprintf(pending->time_text, sizeof(pending->time_text), "%s", time_text);
            pending->can_snooze = can_snooze;
        }
        return;
    }
    bool sound_available = hachimi_alert_start();
    lv_obj_t * overlay = lv_obj_create(lv_layer_top());
    ring_dialog = overlay;
    lv_obj_remove_style_all(overlay);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_size(overlay, 800, 480);
    lv_obj_set_style_bg_color(overlay, color(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, 190, LV_PART_MAIN);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * card = make_panel(overlay, 190, 91, 420, 298, CLOCK_CARD, 24);
    make_label(card, title, 24, 24, 372, 36, CLOCK_SECONDARY, 18, LV_TEXT_ALIGN_CENTER);
    make_label(card, time_text, 24, 72, 372, 64, 0xffffff, 42, LV_TEXT_ALIGN_CENTER);
    make_label(card, sound_available ? "轻点按钮关闭提醒" : "声音输出不可用，请查看提醒", 24, 141, 372, 26,
               CLOCK_SECONDARY, 16, LV_TEXT_ALIGN_CENTER);
    if(can_snooze) {
        make_button(card, "稍后 9 分钟", 30, 210, 170, 54, 0x3a3a3c,
                    CLOCK_BLUE, 16, ring_dialog_event_cb, (void *)(uintptr_t)1);
        make_button(card, "停止", 220, 210, 170, 54, CLOCK_ORANGE,
                    0xffffff, 18, ring_dialog_event_cb, (void *)(uintptr_t)2);
    }
    else {
        make_button(card, "知道了", 118, 210, 184, 54, CLOCK_ORANGE,
                    0xffffff, 18, ring_dialog_event_cb, (void *)(uintptr_t)2);
    }
}

static void check_alarms(void)
{
    bool changed = false;
    time_t now = time(NULL);
    struct tm * local_pointer = localtime(&now);
    if(local_pointer == NULL) return;
    struct tm local = *local_pointer;
    int today = date_key(&local);

    for(size_t i = 0; i < CLOCK_ALARM_CAPACITY + 1; i++) {
        if(snooze_due[i] == 0 || now < snooze_due[i]) continue;
        snooze_due[i] = 0;
        char time_text[16];
        format_clock_time(&local, time_text, sizeof(time_text));
        show_ring_dialog("稍后提醒", time_text, true);
    }

    for(size_t i = 0; i < alarm_count; i++) {
        clock_alarm_t * alarm = &alarms[i];
        if(!alarm->enabled || !alarm_day_matches(alarm, &local) ||
           alarm->last_fired_date == today || alarm->hour != local.tm_hour ||
           alarm->minute != local.tm_min) continue;
        alarm->last_fired_date = today;
        changed = true;
        if(alarm->repeat_days == 0) {
            alarm->enabled = false;
        }
        char time_text[16];
        format_clock_time(&local, time_text, sizeof(time_text));
        show_ring_dialog("闹钟", time_text, true);
    }

    if(changed) {
        save_alarms();
        if(page_area != NULL && current_page == CLOCK_PAGE_ALARMS) render_current_page();
    }
}

static void runtime_tick_cb(lv_timer_t * timer)
{
    (void)timer;
    hachimi_alert_poll();
    if(stopwatch_running) {
        stopwatch_accumulated_ms += lv_tick_elaps(stopwatch_started_tick);
        stopwatch_started_tick = lv_tick_get();
    }

    if(timer_running) {
        uint32_t now = lv_tick_get();
        uint32_t elapsed = lv_tick_elaps(timer_last_tick);
        timer_last_tick = now;
        if((uint64_t)elapsed >= timer_remaining_ms) {
            timer_remaining_ms = 0;
            timer_running = false;
            timer_paused = false;
            show_ring_dialog("计时结束", "00:00:00", false);
            if(page_area != NULL && current_page == CLOCK_PAGE_TIMER) render_current_page();
        }
        else {
            timer_remaining_ms -= elapsed;
        }
    }

    static uint32_t refresh_count;
    refresh_count++;
    if(refresh_count % 10 == 0) check_alarms();
}

static void clock_tick_cb(lv_timer_t * timer)
{
    (void)timer;
    update_stopwatch_display();
    update_timer_display();
    static unsigned refresh_count;
    if(++refresh_count % 20 == 0) refresh_world_clock();
}

static void update_navigation_styles(void)
{
    for(int i = 0; i < CLOCK_PAGE_COUNT; i++) {
        if(navigation_labels[i] == NULL) continue;
        lv_obj_set_style_text_color(navigation_labels[i],
                                    color(current_page == (clock_page_t)i ? CLOCK_ORANGE : 0xe5e5ea),
                                    LV_PART_MAIN);
        lv_obj_t * button = lv_obj_get_parent(navigation_labels[i]);
        lv_obj_set_style_bg_color(button,
                                  color(current_page == (clock_page_t)i ? 0x343437 : CLOCK_SIDEBAR),
                                  LV_PART_MAIN);
    }
}

static void render_world_page(void)
{
    make_label(page_area, "世界时钟", 18, 4, 460, 34,
               0xffffff, 22, LV_TEXT_ALIGN_LEFT);
    make_label(page_area, "本地时间与其他城市", 18, 38, 460, 20,
               CLOCK_SECONDARY, 14, LV_TEXT_ALIGN_LEFT);

    for(size_t i = 0; i < CLOCK_WORLD_CITY_COUNT + 1; i++) {
        lv_coord_t y = 71 + (lv_coord_t)i * 51;
        make_label(page_area, i == 0 ? "本地" : world_cities[i - 1].name, 20, y + 3, 150, 24,
                   i == 0 ? CLOCK_ORANGE : 0xffffff, 18, LV_TEXT_ALIGN_LEFT);
        world_date_labels[i] = make_label(page_area, "", 20, y + 28, 315, 18,
                                          CLOCK_SECONDARY, 12, LV_TEXT_ALIGN_LEFT);
        world_time_labels[i] = make_label(page_area, "--:--", 356, y, 200, 42,
                                          0xffffff, 32, LV_TEXT_ALIGN_RIGHT);
        if(i < CLOCK_WORLD_CITY_COUNT) make_separator(page_area, y + 48, 20, 536);
    }
    refresh_world_clock();
}

static void format_alarm_repeat_days(uint8_t mask, char * output, size_t capacity)
{
    if(mask == 0) {
        snprintf(output, capacity, "仅一次");
        return;
    }
    if(mask == 127) {
        snprintf(output, capacity, "每天");
        return;
    }
    size_t used = 0;
    output[0] = '\0';
    for(size_t i = 0; i < 7; i++) {
        if((mask & weekday_bits_ui[i]) == 0) continue;
        int written = snprintf(output + used, capacity - used,
                               "%s周%s", used == 0 ? "" : " ", weekday_short[(i + 1) % 7]);
        if(written < 0 || (size_t)written >= capacity - used) break;
        used += (size_t)written;
    }
}

static void alarm_row_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);
    if(index >= alarm_count) return;
    lv_obj_t * toggle = lv_event_get_target(event);
    alarms[index].enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    if(alarm_time_labels[index] != NULL) {
        lv_obj_set_style_text_color(alarm_time_labels[index],
                                    color(alarms[index].enabled ? 0xffffff : CLOCK_SECONDARY), 0);
    }
    save_alarms();
}

static void alarm_delete_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);
    if(index >= alarm_count) return;
    for(size_t i = index; i + 1 < alarm_count; i++) alarms[i] = alarms[i + 1];
    alarm_count--;
    save_alarms();
    render_current_page();
}

static void alarm_edit_mode_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    alarm_edit_mode = !alarm_edit_mode;
    render_current_page();
}

static void close_alarm_editor(void)
{
    if(alarm_editor != NULL) {
        lv_obj_delete(alarm_editor);
        alarm_editor = NULL;
        editor_hour_roller = editor_minute_roller = NULL;
    }
}

static void alarm_editor_weekday_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    uintptr_t ui_index = (uintptr_t)lv_event_get_user_data(event);
    if(ui_index >= 7) return;
    uint8_t bit = weekday_bits_ui[ui_index];
    editor_repeat_days ^= bit;
    lv_obj_t * button = lv_event_get_target(event);
    bool selected = (editor_repeat_days & bit) != 0;
    lv_obj_set_style_bg_color(button, color(selected ? CLOCK_ORANGE : 0x343437), LV_PART_MAIN);
    lv_obj_set_style_text_color(lv_obj_get_child(button, 0),
                                color(selected ? 0xffffff : 0xe5e5ea), LV_PART_MAIN);
}

static void alarm_editor_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    uintptr_t action = (uintptr_t)lv_event_get_user_data(event);
    if(action == 1 && (editor_alarm_index >= 0 || alarm_count < CLOCK_ALARM_CAPACITY)) {
        size_t index = editor_alarm_index >= 0 ? (size_t)editor_alarm_index : alarm_count++;
        alarms[index].hour = (uint8_t)lv_roller_get_selected(editor_hour_roller);
        alarms[index].minute = (uint8_t)lv_roller_get_selected(editor_minute_roller);
        alarms[index].repeat_days = editor_repeat_days;
        alarms[index].enabled = true;
        alarms[index].last_fired_date = 0;
        save_alarms();
    }
    close_alarm_editor();
    render_current_page();
}

static void open_alarm_editor(int index)
{
    if(alarm_editor != NULL || (index < 0 && alarm_count >= CLOCK_ALARM_CAPACITY)) return;
    if(index >= (int)alarm_count) return;
    editor_alarm_index = index;
    editor_repeat_days = index < 0 ? 0 : alarms[index].repeat_days;
    lv_obj_t * overlay = lv_obj_create(clock_root);
    alarm_editor = overlay;
    lv_obj_remove_style_all(overlay);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_size(overlay, 800, 480);
    lv_obj_set_style_bg_color(overlay, color(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, 190, LV_PART_MAIN);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * card = make_panel(overlay, 150, 61, 500, 358, CLOCK_CARD, 22);
    make_label(card, index < 0 ? "添加闹钟" : "编辑闹钟", 24, 16, 452, 31, 0xffffff, 22, LV_TEXT_ALIGN_CENTER);
    make_label(card, "时", 135, 53, 70, 22, CLOCK_SECONDARY, 14, LV_TEXT_ALIGN_CENTER);
    make_label(card, "分", 295, 53, 70, 22, CLOCK_SECONDARY, 14, LV_TEXT_ALIGN_CENTER);

    char hour_options[96];
    char minute_options[240];
    size_t hour_used = 0;
    size_t minute_used = 0;
    for(unsigned i = 0; i < 24; i++) {
        int count = snprintf(hour_options + hour_used, sizeof(hour_options) - hour_used,
                             "%s%02u", i == 0 ? "" : "\n", i);
        if(count > 0) hour_used += (size_t)count;
    }
    for(unsigned i = 0; i < 60; i++) {
        int count = snprintf(minute_options + minute_used, sizeof(minute_options) - minute_used,
                             "%s%02u", i == 0 ? "" : "\n", i);
        if(count > 0) minute_used += (size_t)count;
    }
    lv_obj_t * hour_roller = lv_roller_create(card);
    editor_hour_roller = hour_roller;
    lv_obj_set_pos(hour_roller, 112, 77);
    lv_obj_set_size(hour_roller, 105, 120);
    lv_roller_set_options(hour_roller, hour_options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(hour_roller, 3);
    lv_obj_t * minute_roller = lv_roller_create(card);
    editor_minute_roller = minute_roller;
    lv_obj_set_pos(minute_roller, 275, 77);
    lv_obj_set_size(minute_roller, 105, 120);
    lv_roller_set_options(minute_roller, minute_options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(minute_roller, 3);
    lv_obj_t * rollers[] = {hour_roller, minute_roller};
    for(size_t i = 0; i < 2; i++) {
        lv_obj_set_style_bg_color(rollers[i], color(0x343437), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(rollers[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_text_color(rollers[i], color(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_text_font(rollers[i], font(22), LV_PART_MAIN);
        lv_obj_set_style_text_color(rollers[i], color(CLOCK_ORANGE), LV_PART_SELECTED);
        lv_obj_set_style_bg_color(rollers[i], color(0x3a3a3c), LV_PART_SELECTED);
        lv_obj_set_style_text_font(rollers[i], font(22), LV_PART_SELECTED);
        lv_obj_set_style_border_width(rollers[i], 0, LV_PART_MAIN);
        lv_roller_set_visible_row_count(rollers[i], 3);
    }
    time_t now = time(NULL) + 60;
    struct tm * default_time_ptr = localtime(&now);
    if(default_time_ptr != NULL) {
        lv_roller_set_selected(hour_roller, index < 0 ? (uint32_t)default_time_ptr->tm_hour : alarms[index].hour, LV_ANIM_OFF);
        lv_roller_set_selected(minute_roller, index < 0 ? (uint32_t)default_time_ptr->tm_min : alarms[index].minute, LV_ANIM_OFF);
    }

    make_label(card, "重复", 28, 210, 444, 22,
               CLOCK_SECONDARY, 14, LV_TEXT_ALIGN_LEFT);
    for(size_t i = 0; i < 7; i++) {
        char day_text[8];
        snprintf(day_text, sizeof(day_text), "周%s", weekday_short[(i + 1) % 7]);
        lv_obj_t * button = make_button(card, day_text, 33 + (lv_coord_t)i * 62, 237,
                                        54, 40, (editor_repeat_days & weekday_bits_ui[i]) ? CLOCK_ORANGE : 0x343437, 0xe5e5ea, 14,
                                        alarm_editor_weekday_event_cb, (void *)(uintptr_t)i);
        lv_obj_set_style_radius(button, 20, LV_PART_MAIN);
    }
    make_button(card, "取消", 55, 297, 165, 46, 0x3a3a3c,
                0xffffff, 16, alarm_editor_event_cb, (void *)(uintptr_t)0);
    make_button(card, "存储", 280, 297, 165, 46, CLOCK_ORANGE,
                0xffffff, 16, alarm_editor_event_cb, (void *)(uintptr_t)1);
}

static void update_alarm_list_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    open_alarm_editor(-1);
}

static void edit_alarm_event_cb(lv_event_t * event)
{
    open_alarm_editor((int)(uintptr_t)lv_event_get_user_data(event));
}

static void render_alarms_page(void)
{
    make_label(page_area, "闹钟", 18, 4, 300, 34,
               0xffffff, 22, LV_TEXT_ALIGN_LEFT);
    make_button(page_area, alarm_edit_mode ? "完成" : "编辑", 438, 4, 64, 36,
                CLOCK_BG, CLOCK_ORANGE, 14, alarm_edit_mode_event_cb, NULL);
    make_button(page_area, "+", 520, 4, 44, 36, CLOCK_ORANGE,
                0xffffff, 28, update_alarm_list_event_cb, NULL);

    if(alarm_count == 0) {
        make_label(page_area, "没有闹钟", 30, 150, 520, 35,
                   CLOCK_SECONDARY, 22, LV_TEXT_ALIGN_CENTER);
        make_label(page_area, "轻点右上角的 + 添加一个闹钟", 30, 193, 520, 24,
                   CLOCK_SECONDARY, 14, LV_TEXT_ALIGN_CENTER);
        return;
    }

    lv_obj_t * list = lv_obj_create(page_area);
    lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, 10, 50);
    lv_obj_set_size(list, 570, 332);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    for(size_t i = 0; i < alarm_count; i++) {
        clock_alarm_t * alarm = &alarms[i];
        lv_coord_t y = (lv_coord_t)i * 57;
        char time_text[8];
        char repeat_text[96];
        snprintf(time_text, sizeof(time_text), "%02u:%02u", alarm->hour, alarm->minute);
        format_alarm_repeat_days(alarm->repeat_days, repeat_text, sizeof(repeat_text));

        lv_obj_t * time_label = make_label(list, time_text, 55, y + 2, 250, 37,
                                           alarm->enabled ? 0xffffff : CLOCK_SECONDARY,
                                           32, LV_TEXT_ALIGN_LEFT);
        lv_obj_t * repeat_label = make_label(list, repeat_text, 57, y + 38, 335, 17,
                                             CLOCK_SECONDARY, 12, LV_TEXT_ALIGN_LEFT);
        alarm_time_labels[i] = time_label;
        lv_obj_add_flag(time_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(time_label, edit_alarm_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        (void)repeat_label;

        if(alarm_edit_mode) {
            make_button(list, "−", 4, y + 9, 36, 36, CLOCK_RED,
                        0xffffff, 22, alarm_delete_event_cb, (void *)(uintptr_t)i);
        }
        else {
            lv_obj_t * toggle = lv_switch_create(list);
            lv_obj_set_pos(toggle, 493, y + 12);
            lv_obj_set_size(toggle, 54, 30);
            lv_obj_set_style_bg_color(toggle, color(0x39393d), LV_PART_MAIN);
            lv_obj_set_style_bg_color(toggle, color(CLOCK_GREEN), LV_PART_INDICATOR | LV_STATE_CHECKED);
            lv_obj_set_style_bg_color(toggle, color(0xffffff), LV_PART_KNOB);
            if(alarm->enabled) lv_obj_add_state(toggle, LV_STATE_CHECKED);
            lv_obj_add_event_cb(toggle, alarm_row_event_cb, LV_EVENT_VALUE_CHANGED,
                                (void *)(uintptr_t)i);
        }
        make_separator(list, y + 55, 48, 515);
    }
}

static void stopwatch_button_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    uintptr_t action = (uintptr_t)lv_event_get_user_data(event);
    if(action == 1) {
        if(stopwatch_running) {
            uint64_t elapsed = stopwatch_elapsed_now();
            if(stopwatch_lap_count < CLOCK_LAP_CAPACITY) {
                uint64_t previous = stopwatch_lap_count == 0 ? 0 :
                                    stopwatch_laps[stopwatch_lap_count - 1].elapsed_ms;
                stopwatch_laps[stopwatch_lap_count].elapsed_ms = elapsed;
                stopwatch_laps[stopwatch_lap_count].split_ms = elapsed - previous;
                stopwatch_lap_count++;
                render_current_page();
            }
        }
        else {
            stopwatch_accumulated_ms = 0;
            stopwatch_lap_count = 0;
            render_current_page();
        }
    }
    else if(action == 2) {
        if(stopwatch_running) {
            stopwatch_accumulated_ms = stopwatch_elapsed_now();
            stopwatch_running = false;
        }
        else {
            stopwatch_started_tick = lv_tick_get();
            stopwatch_running = true;
        }
        render_current_page();
    }
}

static void render_stopwatch_page(void)
{
    make_label(page_area, "秒表", 18, 4, 300, 34,
               0xffffff, 22, LV_TEXT_ALIGN_LEFT);
    make_label(page_area, "计时精确到百分之一秒", 18, 38, 450, 20,
               CLOCK_SECONDARY, 14, LV_TEXT_ALIGN_LEFT);

    stopwatch_label = make_label(page_area, "00:00.00", 14, 78, 556, 88,
                                 0xffffff, 64, LV_TEXT_ALIGN_CENTER);
    make_button(page_area, stopwatch_running ? "计次" : "归零",
                100, 172, 76, 76, 0x3a3a3c, 0xffffff, 16,
                stopwatch_button_event_cb, (void *)(uintptr_t)1);
    make_button(page_area, stopwatch_running ? "停止" : "开始",
                408, 172, 76, 76,
                stopwatch_running ? CLOCK_RED : CLOCK_GREEN,
                0xffffff, 18, stopwatch_button_event_cb, (void *)(uintptr_t)2);

    stopwatch_lap_list = lv_obj_create(page_area);
    lv_obj_remove_style_all(stopwatch_lap_list);
    lv_obj_set_pos(stopwatch_lap_list, 20, 257);
    lv_obj_set_size(stopwatch_lap_list, 544, 133);
    lv_obj_set_style_bg_color(stopwatch_lap_list, color(CLOCK_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(stopwatch_lap_list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(stopwatch_lap_list, 15, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(stopwatch_lap_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(stopwatch_lap_list, LV_DIR_VER);
    if(stopwatch_lap_count == 0) {
        make_label(stopwatch_lap_list, "圈次会显示在这里", 16, 52, 512, 24,
                   CLOCK_SECONDARY, 14, LV_TEXT_ALIGN_CENTER);
    }
    else {
        for(size_t i = 0; i < stopwatch_lap_count; i++) {
            size_t reverse_index = stopwatch_lap_count - 1 - i;
            char split[32];
            char line[48];
            format_duration(stopwatch_laps[reverse_index].split_ms, true, split, sizeof(split));
            snprintf(line, sizeof(line), "第%u圈", (unsigned)reverse_index + 1);
            lv_coord_t y = (lv_coord_t)i * 34;
            make_label(stopwatch_lap_list, line, 18, y + 4, 240, 25,
                       CLOCK_SECONDARY, 14, LV_TEXT_ALIGN_LEFT);
            make_label(stopwatch_lap_list, split, 275, y + 4, 245, 25,
                       0xffffff, 16, LV_TEXT_ALIGN_RIGHT);
            if(i + 1 < stopwatch_lap_count) make_separator(stopwatch_lap_list, y + 33, 15, 514);
        }
    }
    update_stopwatch_display();
}

static void update_timer_roller_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    for(size_t i = 0; i < 3; i++) {
        if(timer_rollers[i] != NULL) timer_selection[i] = lv_roller_get_selected(timer_rollers[i]);
    }
    update_timer_display();
}

static void timer_button_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    uintptr_t action = (uintptr_t)lv_event_get_user_data(event);
    if(action == 1) {
        if(timer_running) {
            timer_running = false;
            timer_paused = true;
        }
        else if(timer_paused) {
            timer_running = true;
            timer_paused = false;
            timer_last_tick = lv_tick_get();
        }
        else if(timer_rollers[0] != NULL) {
            uint64_t hours = lv_roller_get_selected(timer_rollers[0]);
            uint64_t minutes = lv_roller_get_selected(timer_rollers[1]);
            uint64_t seconds = lv_roller_get_selected(timer_rollers[2]);
            timer_remaining_ms = (hours * 3600 + minutes * 60 + seconds) * 1000;
            if(timer_remaining_ms == 0) return;
            timer_running = true;
            timer_paused = false;
            timer_last_tick = lv_tick_get();
        }
        render_current_page();
    }
    else if(action == 2) {
        timer_running = false;
        timer_paused = false;
        timer_remaining_ms = 0;
        render_current_page();
    }
}

static void render_timer_page(void)
{
    make_label(page_area, "计时器", 18, 4, 300, 34,
               0xffffff, 22, LV_TEXT_ALIGN_LEFT);
    make_label(page_area, "倒数结束时提醒", 18, 38, 450, 20,
               CLOCK_SECONDARY, 14, LV_TEXT_ALIGN_LEFT);

    timer_value_label = make_label(page_area, "00:05:00", 20, 68, 552, 72,
                                   0xffffff, 56, LV_TEXT_ALIGN_CENTER);
    timer_picker_panel = lv_obj_create(page_area);
    lv_obj_remove_style_all(timer_picker_panel);
    lv_obj_set_pos(timer_picker_panel, 48, 133);
    lv_obj_set_size(timer_picker_panel, 500, 188);
    lv_obj_clear_flag(timer_picker_panel, LV_OBJ_FLAG_SCROLLABLE);

    static const char * const options[] = {
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23",
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n50\n51\n52\n53\n54\n55\n56\n57\n58\n59"
    };
    const lv_coord_t x[] = {65, 212, 359};
    const char * unit[] = {"时", "分", "秒"};
    for(size_t i = 0; i < 3; i++) {
        make_label(timer_picker_panel, unit[i], x[i], 0, 76, 20,
                   CLOCK_SECONDARY, 14, LV_TEXT_ALIGN_CENTER);
        timer_rollers[i] = lv_roller_create(timer_picker_panel);
        lv_obj_set_pos(timer_rollers[i], x[i], 25);
        lv_obj_set_size(timer_rollers[i], 76, 155);
        lv_roller_set_options(timer_rollers[i], options[i == 0 ? 0 : 1], LV_ROLLER_MODE_NORMAL);
        lv_roller_set_visible_row_count(timer_rollers[i], 3);
        lv_obj_set_style_bg_color(timer_rollers[i], color(CLOCK_CARD), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(timer_rollers[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_text_color(timer_rollers[i], color(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_text_font(timer_rollers[i], font(22), LV_PART_MAIN);
        lv_obj_set_style_text_color(timer_rollers[i], color(CLOCK_ORANGE), LV_PART_SELECTED);
        lv_obj_set_style_bg_color(timer_rollers[i], color(0x343437), LV_PART_SELECTED);
        lv_obj_set_style_text_font(timer_rollers[i], font(22), LV_PART_SELECTED);
        lv_obj_set_style_border_width(timer_rollers[i], 0, LV_PART_MAIN);
        lv_roller_set_visible_row_count(timer_rollers[i], 3);
        lv_obj_add_event_cb(timer_rollers[i], update_timer_roller_event_cb,
                            LV_EVENT_VALUE_CHANGED, NULL);
    }
    for(size_t i = 0; i < 3; i++) lv_roller_set_selected(timer_rollers[i], timer_selection[i], LV_ANIM_OFF);

    bool selecting = !timer_running && !timer_paused;
    if(!selecting) lv_obj_add_flag(timer_picker_panel, LV_OBJ_FLAG_HIDDEN);
    make_button(page_area, timer_running ? "暂停" : (timer_paused ? "继续" : "开始"),
                160, 338, 130, 48, timer_running ? CLOCK_ORANGE : CLOCK_GREEN,
                0xffffff, 18, timer_button_event_cb, (void *)(uintptr_t)1);
    if(!selecting) {
        make_button(page_area, "取消", 332, 338, 130, 48, 0x3a3a3c,
                    0xffffff, 18, timer_button_event_cb, (void *)(uintptr_t)2);
    }
    update_timer_display();
}

static void render_current_page(void)
{
    if(page_area == NULL) return;
    lv_obj_clean(page_area);
    memset(world_time_labels, 0, sizeof(world_time_labels));
    memset(world_date_labels, 0, sizeof(world_date_labels));
    memset(timer_rollers, 0, sizeof(timer_rollers));
    memset(alarm_time_labels, 0, sizeof(alarm_time_labels));
    stopwatch_label = NULL;
    stopwatch_lap_list = NULL;
    timer_value_label = NULL;
    timer_picker_panel = NULL;
    update_navigation_styles();

    switch(current_page) {
        case CLOCK_PAGE_WORLD: render_world_page(); break;
        case CLOCK_PAGE_ALARMS: render_alarms_page(); break;
        case CLOCK_PAGE_STOPWATCH: render_stopwatch_page(); break;
        case CLOCK_PAGE_TIMER: render_timer_page(); break;
        default: break;
    }
}

static void navigation_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    uintptr_t page = (uintptr_t)lv_event_get_user_data(event);
    if(page >= CLOCK_PAGE_COUNT) return;
    current_page = (clock_page_t)page;
    render_current_page();
}

static void clock_root_deleted(lv_event_t * event)
{
    (void)event;
    if(clock_timer != NULL) {
        lv_timer_delete(clock_timer);
        clock_timer = NULL;
    }
    alarm_editor = NULL;
    editor_hour_roller = editor_minute_roller = NULL;
    clock_root = NULL;
    page_area = NULL;
    navigation_labels[0] = navigation_labels[1] = NULL;
    navigation_labels[2] = navigation_labels[3] = NULL;
    memset(world_time_labels, 0, sizeof(world_time_labels));
    memset(world_date_labels, 0, sizeof(world_date_labels));
    memset(alarm_time_labels, 0, sizeof(alarm_time_labels));
    stopwatch_label = NULL;
    stopwatch_lap_list = NULL;
    timer_value_label = NULL;
    timer_picker_panel = NULL;
    memset(timer_rollers, 0, sizeof(timer_rollers));
}

static lv_obj_t * clock_app_create(lv_obj_t * parent)
{
    load_alarms();
    current_page = CLOCK_PAGE_WORLD;
    alarm_edit_mode = false;
    memset(navigation_labels, 0, sizeof(navigation_labels));

    clock_root = lv_obj_create(parent);
    lv_obj_remove_style_all(clock_root);
    lv_obj_set_pos(clock_root, 0, 0);
    lv_obj_set_size(clock_root, 800, 480);
    lv_obj_set_style_bg_color(clock_root, color(CLOCK_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(clock_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(clock_root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(clock_root, LV_OBJ_FLAG_SCROLLABLE);

    make_panel(clock_root, 0, 0, 182, 480, CLOCK_SIDEBAR, 0);
    make_label(clock_root, "时钟", 23, 58, 136, 34,
               0xffffff, 22, LV_TEXT_ALIGN_LEFT);
    for(int i = 0; i < CLOCK_PAGE_COUNT; i++) {
        lv_obj_t * button = lv_obj_create(clock_root);
        lv_obj_remove_style_all(button);
        lv_obj_set_pos(button, 15, 102 + i * 54);
        lv_obj_set_size(button, 152, 44);
        lv_obj_set_style_radius(button, 11, LV_PART_MAIN);
        lv_obj_set_style_bg_color(button, color(CLOCK_SIDEBAR), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(button, navigation_event_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
        navigation_labels[i] = make_label(button, nav_titles[i], 15, 10, 124, 26,
                                          0xe5e5ea, 18, LV_TEXT_ALIGN_LEFT);
    }
    make_panel(clock_root, 181, 0, 1, 480, CLOCK_SEPARATOR, 0);

    page_area = lv_obj_create(clock_root);
    lv_obj_remove_style_all(page_area);
    lv_obj_set_pos(page_area, 194, 54);
    lv_obj_set_size(page_area, 594, 390);
    lv_obj_clear_flag(page_area, LV_OBJ_FLAG_SCROLLABLE);
    render_current_page();

    ios_status_bar_create(clock_root, lv_color_white());
    lv_obj_add_event_cb(clock_root, clock_root_deleted, LV_EVENT_DELETE, NULL);
    clock_timer = lv_timer_create(clock_tick_cb, 50, NULL);
    lv_obj_add_flag(clock_root, LV_OBJ_FLAG_HIDDEN);
    return clock_root;
}

static void clock_app_open(lv_obj_t * view)
{
    if(view != clock_root) return;
    if(clock_timer) lv_timer_resume(clock_timer);
    render_current_page();
}

static void clock_app_close(lv_obj_t * view)
{
    if(view == clock_root) {
        close_alarm_editor();
        if(clock_timer) lv_timer_pause(clock_timer);
    }
}

const hachimi_app_module_t hachimi_clock_app = {
    "clock", clock_app_create, clock_app_open, clock_app_close,
    hachimi_clock_runtime_start, hachimi_clock_runtime_stop
};

void hachimi_clock_runtime_start(void)
{
    if(runtime_timer != NULL) return;
    load_alarms();
    runtime_timer = lv_timer_create(runtime_tick_cb, 50, NULL);
    check_alarms();
}

void hachimi_clock_runtime_stop(void)
{
    if(runtime_timer != NULL) {
        lv_timer_delete(runtime_timer);
        runtime_timer = NULL;
    }
    close_ring_dialog();
    memset(snooze_due, 0, sizeof(snooze_due));
    pending_alert_count = 0;
    timer_running = timer_paused = false;
    timer_remaining_ms = 0;
    stopwatch_accumulated_ms = stopwatch_elapsed_now();
    stopwatch_running = false;
    alarm_storage_loaded = false;
    alarm_storage_path[0] = '\0';
}
