#include "ios_display.h"

#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "services/display.h"
#define MIN_BRIGHTNESS_PERCENT HACHIMI_DISPLAY_MIN_PERCENT
#define IOS_ASSET(name) "A:assets/ios/" name

static lv_obj_t * display_root;
static lv_obj_t * brightness_slider;
static int current_percent = 80;
static bool updating_slider;

static void brightness_changed_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED || updating_slider) return;
    int percent = lv_slider_get_value(brightness_slider);
    if(hachimi_display_set_brightness(percent)) current_percent = percent;
    else (void)hachimi_display_get_brightness(&current_percent);
    updating_slider = true;
    lv_slider_set_value(brightness_slider, current_percent, LV_ANIM_OFF);
    updating_slider = false;
}

void ios_display_refresh(void)
{
    if(display_root == NULL) return;
    bool available = hachimi_display_get_brightness(&current_percent);
    if(available) lv_obj_clear_state(brightness_slider, LV_STATE_DISABLED);
    else { current_percent = 80; lv_obj_add_state(brightness_slider, LV_STATE_DISABLED); }
    updating_slider = true;
    lv_slider_set_value(brightness_slider, current_percent, LV_ANIM_OFF);
    updating_slider = false;
}

static lv_obj_t * create_display_label(lv_obj_t * parent, const char * text,
                                      lv_coord_t x, lv_coord_t y,
                                      lv_coord_t width, lv_coord_t height,
                                      lv_color_t color, const lv_font_t * font)
{
    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(label, 0, LV_PART_MAIN);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return label;
}

static lv_obj_t * create_display_card(lv_obj_t * parent, lv_coord_t x, lv_coord_t y,
                                      lv_coord_t width, lv_coord_t height)
{
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, width, height);
    lv_obj_set_style_radius(card, 15, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static void create_sun_mark(lv_obj_t * parent, lv_coord_t x, lv_coord_t y,
                            lv_coord_t size, lv_color_t color)
{
    lv_obj_t * mark = lv_obj_create(parent);
    lv_obj_remove_style_all(mark);
    lv_obj_set_pos(mark, x, y);
    lv_obj_set_size(mark, size, size);
    lv_obj_clear_flag(mark, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_coord_t ray_width = size >= 16 ? 2 : 1;
    lv_coord_t ray_length = size >= 16 ? 4 : 2;
    lv_coord_t core_size = size >= 16 ? 6 : 3;
    float center = (float)size / 2.0f;
    float radius = (float)core_size / 2.0f + (float)size * 0.08f +
                   (float)ray_length / 2.0f;
    for(int i = 0; i < 8; i++) {
        float angle = (float)i * 0.7853981634f;
        float ray_center_x = center + sinf(angle) * radius;
        float ray_center_y = center - cosf(angle) * radius;
        lv_obj_t * ray = lv_obj_create(mark);
        lv_obj_remove_style_all(ray);
        lv_obj_set_size(ray, ray_width, ray_length);
        lv_obj_set_pos(ray, (lv_coord_t)lroundf(ray_center_x - (float)ray_width / 2.0f),
                       (lv_coord_t)lroundf(ray_center_y - (float)ray_length / 2.0f));
        lv_obj_set_style_radius(ray, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(ray, color, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ray, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_transform_pivot_x(ray, ray_width / 2, LV_PART_MAIN);
        lv_obj_set_style_transform_pivot_y(ray, ray_length / 2, LV_PART_MAIN);
        lv_obj_set_style_transform_rotation(ray, i * 450, LV_PART_MAIN);
        lv_obj_clear_flag(ray, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_t * center_dot = lv_obj_create(mark);
    lv_obj_remove_style_all(center_dot);
    lv_obj_set_size(center_dot, core_size, core_size);
    lv_obj_center(center_dot);
    lv_obj_set_style_radius(center_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(center_dot, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(center_dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(center_dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

static void display_deleted(lv_event_t * event)
{
    (void)event;
    display_root = brightness_slider = NULL;
}

void ios_display_create(lv_obj_t * parent, const lv_font_t * font,
                        const lv_font_t * small_font, const lv_font_t * heading_font,
                        const lv_font_t * title_font)
{
    display_root = lv_obj_create(parent);
    lv_obj_add_event_cb(display_root, display_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_remove_style_all(display_root);
    lv_obj_set_pos(display_root, 0, 0);
    lv_obj_set_size(display_root, 582, 452);
    lv_obj_set_style_bg_color(display_root, lv_color_hex(0xf2f2f7), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(display_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(display_root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * intro = create_display_card(display_root, 14, 50, 554, 112);
    lv_obj_t * icon = lv_image_create(intro);
    lv_image_set_src(icon, IOS_ASSET("settings_display.png"));
    lv_obj_set_pos(icon, 14, 12);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    create_display_label(intro, "显示与亮度", 14, 56, 500, 30,
                         lv_color_hex(0x17181b), title_font);
    create_display_label(intro, "调整屏幕亮度，让显示效果适合当前环境。", 14, 88, 520, 20,
                         lv_color_hex(0x85858b), small_font);

    create_display_label(display_root, "亮度", 30, 182, 520, 18,
                         lv_color_hex(0x85858b), small_font);

    lv_obj_t * card = create_display_card(display_root, 14, 208, 554, 96);
    create_display_label(card, "屏幕亮度", 18, 12, 500, 24,
                         lv_color_hex(0x202124), heading_font);

    create_sun_mark(card, 20, 51, 10, lv_color_hex(0x9a9aa0));
    create_sun_mark(card, 522, 47, 18, lv_color_hex(0xff9f0a));
    brightness_slider = lv_slider_create(card);
    lv_obj_set_pos(brightness_slider, 42, 45);
    lv_obj_set_size(brightness_slider, 464, 22);
    lv_slider_set_range(brightness_slider, MIN_BRIGHTNESS_PERCENT, 100);
    lv_obj_set_style_height(brightness_slider, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(0xd6d6dc), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(brightness_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(0x007aff), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(brightness_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_border_color(brightness_slider, lv_color_hex(0x007aff), LV_PART_KNOB);
    lv_obj_set_style_border_width(brightness_slider, 1, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(brightness_slider, 2, LV_PART_KNOB);
    lv_obj_set_style_shadow_opa(brightness_slider, LV_OPA_20, LV_PART_KNOB);
    lv_obj_add_event_cb(brightness_slider, brightness_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    (void)font;
    (void)small_font;
    ios_display_refresh();
}
