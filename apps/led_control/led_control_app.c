#include "led_control_app.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "framework/ui/fonts.h"
#include "framework/ui/ios_status_bar.h"
#include "services/led.h"

#define LED_COUNT 3
#define IOS_ASSET(name) "A:assets/ios/" name


static const char * const channel_names[LED_COUNT] = {"红色", "绿色", "蓝色"};
static const uint32_t channel_color_hex[LED_COUNT] = {
    0xff453a, 0x30d158, 0x0a84ff
};
static lv_obj_t * led_root;
static lv_obj_t * preview_swatch;
static lv_obj_t * preview_hex;
static lv_obj_t * status_label;
static lv_obj_t * channel_sliders[LED_COUNT];
static lv_obj_t * channel_values[LED_COUNT];
static lv_timer_t * led_timer;
static hachimi_led_service_t led;
#define channel_value led.values

static void update_status(void)
{
    static const char * const messages[] = {
        "模拟器预览 · 实体 PWM 仅在开发板上控制", "正在读取板载 PWM…",
        "已授权直连板载 RGB · 拖动实时生效", "已连接板载 PWM · 拖动滑块混合颜色",
        "正在连接板载 RGB…", "颜色已应用到开发板 RGB LED",
        "PWM 操作失败，请运行安装脚本授权 LED 控制", "板载 PWM 返回了无法识别的数据",
        "无法连接 PWM，请安装 LED 控制适配器"
    };
    lv_label_set_text(status_label, messages[led.status]);
}

static lv_obj_t * make_label(lv_obj_t * parent, const char * text,
                             lv_coord_t x, lv_coord_t y, lv_coord_t width,
                             lv_coord_t height, unsigned font_size, uint32_t color)
{
    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, hachimi_ui_font(font_size), LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return label;
}

static lv_obj_t * make_card(lv_obj_t * parent, lv_coord_t x, lv_coord_t y,
                            lv_coord_t width, lv_coord_t height)
{
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, width, height);
    lv_obj_set_style_radius(card, 18, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(card, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(card, LV_OPA_10, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static void update_preview(void)
{
    if(preview_swatch == NULL) return;
    uint8_t red = (uint8_t)channel_value[0];
    uint8_t green = (uint8_t)channel_value[1];
    uint8_t blue = (uint8_t)channel_value[2];
    lv_obj_set_style_bg_color(preview_swatch, lv_color_make(red, green, blue), LV_PART_MAIN);
    lv_label_set_text_fmt(preview_hex, "#%02X%02X%02X", red, green, blue);
    for(int i = 0; i < LED_COUNT; i++) {
        lv_label_set_text_fmt(channel_values[i], "%3d", channel_value[i]);
    }
}

static void slider_changed(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    if(index < 0 || index >= LED_COUNT) return;
    hachimi_led_set(&led, (unsigned)index, lv_slider_get_value(channel_sliders[index]), lv_tick_get());
    update_preview();
    update_status();
}

static void poll_led_job(lv_timer_t * timer)
{
    (void)timer;
    if(!hachimi_led_poll(&led, lv_tick_get())) return;
    for(int i = 0; i < LED_COUNT; i++)
        lv_slider_set_value(channel_sliders[i], channel_value[i], LV_ANIM_OFF);
    update_preview();
    update_status();
}

static void root_deleted(lv_event_t * event)
{
    (void)event;
    if(led_timer != NULL) lv_timer_delete(led_timer);
    led_timer = NULL;
    hachimi_led_close(&led);
    led_root = preview_swatch = preview_hex = status_label = NULL;
    memset(channel_sliders, 0, sizeof(channel_sliders));
    memset(channel_values, 0, sizeof(channel_values));
}

static lv_obj_t * led_control_create(lv_obj_t * parent)
{
    hachimi_led_init(&led);

    led_root = lv_obj_create(parent);
    lv_obj_remove_style_all(led_root);
    lv_obj_set_pos(led_root, 0, 0);
    lv_obj_set_size(led_root, 800, 480);
    lv_obj_set_style_bg_color(led_root, lv_color_hex(0xf2f2f7), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(led_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(led_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(led_root, root_deleted, LV_EVENT_DELETE, NULL);

    make_label(led_root, "RGB 灯光", 38, 45, 420, 34, 26, 0x17181b);
    make_label(led_root, "拖动三个滑块，实时混合开发板上的 RGB 颜色", 40, 82,
               680, 22, 14, 0x85858b);

    lv_obj_t * preview_card = make_card(led_root, 34, 122, 284, 322);
    make_label(preview_card, "颜色预览", 22, 20, 240, 26, 17, 0x202124);
    preview_swatch = lv_obj_create(preview_card);
    lv_obj_remove_style_all(preview_swatch);
    lv_obj_set_pos(preview_swatch, 57, 72);
    lv_obj_set_size(preview_swatch, 170, 126);
    lv_obj_set_style_radius(preview_swatch, 24, LV_PART_MAIN);
    lv_obj_set_style_bg_color(preview_swatch, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(preview_swatch, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(preview_swatch, 22, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(preview_swatch, lv_color_hex(0xc6c9d2), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(preview_swatch, LV_OPA_30, LV_PART_MAIN);
    lv_obj_clear_flag(preview_swatch, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    preview_hex = make_label(preview_card, "#000000", 20, 216, 244, 28, 20, 0x202124);
    lv_obj_set_style_text_align(preview_hex, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    make_label(preview_card, "混色结果", 20, 250, 244, 20, 12, 0x85858b);

    lv_obj_t * controls = make_card(led_root, 342, 122, 424, 322);
    make_label(controls, "通道亮度", 24, 20, 350, 26, 17, 0x202124);
    for(int i = 0; i < LED_COUNT; i++) {
        lv_coord_t row_y = (lv_coord_t)(66 + i * 76);
        lv_obj_t * marker = lv_obj_create(controls);
        lv_obj_remove_style_all(marker);
        lv_obj_set_pos(marker, 24, row_y + 5);
        lv_obj_set_size(marker, 12, 12);
        lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(marker, lv_color_hex(channel_color_hex[i]), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(marker, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        make_label(controls, channel_names[i], 45, row_y, 70, 22, 14, 0x202124);
        channel_values[i] = make_label(controls, "  0", 347, row_y, 42, 22, 14, 0x65656c);
        lv_obj_set_style_text_align(channel_values[i], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

        channel_sliders[i] = lv_slider_create(controls);
        lv_obj_set_pos(channel_sliders[i], 45, row_y + 29);
        lv_obj_set_size(channel_sliders[i], 344, 22);
        lv_slider_set_range(channel_sliders[i], 0, 255);
        lv_slider_set_value(channel_sliders[i], 0, LV_ANIM_OFF);
        lv_obj_set_style_height(channel_sliders[i], 5, LV_PART_MAIN);
        lv_obj_set_style_bg_color(channel_sliders[i], lv_color_hex(0xe2e3e8), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(channel_sliders[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(channel_sliders[i], lv_color_hex(channel_color_hex[i]), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(channel_sliders[i], LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(channel_sliders[i], lv_color_white(), LV_PART_KNOB);
        lv_obj_set_style_border_color(channel_sliders[i], lv_color_hex(channel_color_hex[i]), LV_PART_KNOB);
        lv_obj_set_style_border_width(channel_sliders[i], 1, LV_PART_KNOB);
        lv_obj_set_style_shadow_width(channel_sliders[i], 2, LV_PART_KNOB);
        lv_obj_set_style_shadow_opa(channel_sliders[i], LV_OPA_20, LV_PART_KNOB);
        lv_obj_add_event_cb(channel_sliders[i], slider_changed, LV_EVENT_VALUE_CHANGED,
                            (void *)(intptr_t)i);
    }

    status_label = make_label(led_root, "正在读取板载 PWM…", 38, 452, 728, 18, 12, 0x777780);
    ios_status_bar_create(led_root, lv_color_hex(0x202124));
    update_preview();
    lv_obj_add_flag(led_root, LV_OBJ_FLAG_HIDDEN);
    return led_root;
}

static void led_control_open(lv_obj_t * view)
{
    if(view != led_root) return;
    hachimi_led_open(&led, lv_tick_get());
    for(int i = 0; i < LED_COUNT; i++)
        lv_slider_set_value(channel_sliders[i], channel_value[i], LV_ANIM_OFF);
    update_preview();
    update_status();
    if(led_timer == NULL) led_timer = lv_timer_create(poll_led_job, 25, NULL);
}

static void led_control_close(lv_obj_t * view)
{
    if(view != led_root) return;
    if(led_timer) lv_timer_delete(led_timer);
    led_timer = NULL;
    hachimi_led_close(&led);
}

const hachimi_app_module_t hachimi_led_control_app = {
    "led-control", led_control_create, led_control_open, led_control_close, NULL, NULL
};
