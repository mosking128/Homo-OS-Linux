#ifndef HACHIMI_DEMO_UI_H
#define HACHIMI_DEMO_UI_H

#include "framework/ui/fonts.h"
#include <stdint.h>

#define DEMO_BLUE 0x087ef5
#define DEMO_INK 0x202126
#define DEMO_MUTED 0x8a8d93
#define DEMO_LINE 0xe6e7eb
#define DEMO_BG 0xf5f5f7

static inline lv_obj_t * demo_panel(lv_obj_t * parent, int x, int y, int w, int h,
                                    int radius, uint32_t color)
{
    lv_obj_t * obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static inline lv_obj_t * demo_label(lv_obj_t * parent, const char * text,
                                    int x, int y, int w, int h, unsigned size,
                                    uint32_t color)
{
    lv_obj_t * obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_text_font(obj, hachimi_ui_font(size), 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static inline lv_obj_t * demo_button(lv_obj_t * parent, const char * title,
                                     int x, int y, int w, int h, unsigned size,
                                     uint32_t fg, uint32_t bg, lv_event_cb_t cb,
                                     intptr_t data)
{
    lv_obj_t * obj = lv_button_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, h / 2, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(obj, bg ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xe5efff), LV_STATE_PRESSED);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_t * label = lv_label_create(obj);
    lv_label_set_text(label, title);
    /* The bundled Chinese UI font does not contain LVGL's private-use icon glyphs. */
    const lv_font_t * font = (title && (uint8_t)title[0] == 0xef)
                                 ? &lv_font_montserrat_14 : hachimi_ui_font(size);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(fg), 0);
    lv_obj_center(label);
    if(cb) lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, (void *)data);
    return obj;
}

static inline void demo_enable_event_bubble(lv_obj_t * obj)
{
    if(!obj) return;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    for(unsigned i = 0; i < lv_obj_get_child_count(obj); i++)
        demo_enable_event_bubble(lv_obj_get_child(obj, i));
}

static inline lv_obj_t * demo_avatar(lv_obj_t * parent, const char * initials,
                                     int x, int y, int size, uint32_t color)
{
    lv_obj_t * obj = demo_panel(parent, x, y, size, size, size / 2, color);
    lv_obj_t * text = lv_label_create(obj);
    lv_label_set_text(text, initials);
    lv_obj_set_style_text_font(text, hachimi_ui_font(size > 48 ? 22 : 16), 0);
    lv_obj_set_style_text_color(text, lv_color_white(), 0);
    lv_obj_center(text);
    return obj;
}

static inline void demo_rule(lv_obj_t * parent, int x, int y, int w)
{
    demo_panel(parent, x, y, w, 1, 0, DEMO_LINE);
}

#endif
