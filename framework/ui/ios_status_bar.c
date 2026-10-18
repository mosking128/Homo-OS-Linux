#include "ios_status_bar.h"

#include "fonts.h"
#include "ios_icon_assets.h"
#include "services/clock.h"
#include "services/power.h"

#include <stddef.h>


typedef struct status_watch {
    lv_obj_t * object;
    bool battery;
    struct status_watch * next;
} status_watch_t;

static status_watch_t * watches;
static lv_timer_t * clock_timer;
static const lv_font_t * status_time_font;

static void update_time_labels(lv_timer_t * timer)
{
    (void)timer;
    char clock_text[8];
    bool clock_valid = hachimi_clock_format(clock_text, sizeof(clock_text), "%H:%M");
    hachimi_power_state_t power;
    bool available = hachimi_power_read(&power);
    const lv_image_dsc_t * source = available && (power.external_power || power.charging) ?
        &hachimi_ios_battery_charging_36 : &hachimi_ios_battery_36;
    for(status_watch_t * watch = watches; watch; watch = watch->next) {
        if(watch->battery) {
            if(lv_image_get_src(watch->object) != source) lv_image_set_src(watch->object, source);
        }
        else if(clock_valid) lv_label_set_text(watch->object, clock_text);
    }
}

static void release_watch(lv_event_t * event)
{
    status_watch_t * watch = lv_event_get_user_data(event);
    status_watch_t ** link = &watches;
    while(*link && *link != watch) link = &(*link)->next;
    if(*link) { *link = watch->next; lv_free(watch); }
    if(watches == NULL && clock_timer) {
        lv_timer_delete(clock_timer);
        clock_timer = NULL;
    }
}

static void watch_object(lv_obj_t * object, bool battery)
{
    status_watch_t * watch = lv_malloc(sizeof(*watch));
    LV_ASSERT_MALLOC(watch);
    if(watch == NULL) return;
    watch->object = object;
    watch->battery = battery;
    watch->next = watches;
    watches = watch;
    lv_obj_add_event_cb(object, release_watch, LV_EVENT_DELETE, watch);
    if(clock_timer == NULL) clock_timer = lv_timer_create(update_time_labels, 1000, NULL);
}

void ios_status_bar_create_time_label(lv_obj_t * parent, lv_color_t foreground)
{
    if(status_time_font == NULL) {
        status_time_font = hachimi_ui_font(18);
        if(status_time_font == NULL) status_time_font = &lv_font_montserrat_14;
    }

    lv_obj_t * label = lv_label_create(parent);
    lv_obj_set_pos(label, 16, 10);
    lv_obj_set_size(label, 58, 20);
    lv_obj_set_style_text_font(label, status_time_font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, foreground, LV_PART_MAIN);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    watch_object(label, false);
    update_time_labels(NULL);
}

void ios_status_bar_create(lv_obj_t * parent, lv_color_t foreground)
{
    ios_status_bar_create_time_label(parent, foreground);
    const lv_image_dsc_t * assets[] = {&hachimi_ios_wifi_26, &hachimi_ios_battery_36};
    const lv_coord_t x[] = {716, 748};
    const lv_coord_t y[] = {9, 4};
    for(size_t i = 0; i < 2; i++) {
        lv_obj_t * icon = lv_image_create(parent);
        lv_image_set_src(icon, assets[i]);
        lv_image_set_scale(icon, 256);
        lv_obj_set_pos(icon, x[i], y[i]);
        lv_obj_set_style_image_recolor(icon, foreground, LV_PART_MAIN);
        lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        if(i == 1) watch_object(icon, true);
    }
    update_time_labels(NULL);
}
