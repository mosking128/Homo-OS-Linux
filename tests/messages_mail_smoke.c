#include "framework/desktop.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#endif

#define CHECK(c) do { if(!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while(0)
static uint16_t pixels[800 * 480];
static uint8_t buffer[800 * 80 * 2];

static uint32_t ticks(void)
{
#ifdef _WIN32
    return GetTickCount();
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)((uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000);
#endif
}
static void flush(lv_display_t * display, const lv_area_t * area, uint8_t * data)
{
    unsigned width = area->x2 - area->x1 + 1;
    for(int y = area->y1; y <= area->y2; y++)
        memcpy(pixels + y * 800 + area->x1, data + (y - area->y1) * width * 2, width * 2);
    lv_display_flush_ready(display);
}
static void pump(unsigned duration)
{
    uint32_t start = ticks();
    do {
        lv_timer_handler();
#ifdef _WIN32
        Sleep(3);
#else
        usleep(3000);
#endif
    } while(ticks() - start < duration);
}
static lv_obj_t * find_text(lv_obj_t * parent, const char * text)
{
    if(!lv_obj_is_visible(parent)) return NULL;
    if(lv_obj_check_type(parent, &lv_label_class) && !strcmp(lv_label_get_text(parent), text)) return parent;
    for(unsigned i = 0; i < lv_obj_get_child_count(parent); i++) {
        lv_obj_t * found = find_text(lv_obj_get_child(parent, i), text);
        if(found) return found;
    }
    return NULL;
}
static void expect_text(const char * text)
{
    lv_obj_update_layout(lv_screen_active());
    if(!find_text(lv_screen_active(), text)) fprintf(stderr, "Missing visible text: %s\n", text);
    CHECK(find_text(lv_screen_active(), text));
}
static void click_text(const char * text)
{
    lv_obj_t * target = find_text(lv_screen_active(), text);
    CHECK(target);
    while(target && !lv_obj_has_flag(target, LV_OBJ_FLAG_CLICKABLE)) target = lv_obj_get_parent(target);
    CHECK(target);
    lv_obj_send_event(target, LV_EVENT_CLICKED, NULL);
    pump(50);
}
int main(void)
{
    lv_init();
    lv_tick_set_cb(ticks);
    lv_display_t * display = lv_display_create(800, 480);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    ios_desktop_create();
    pump(80);

    CHECK(hachimi_desktop_launch(HACHIMI_APP_MESSAGES));
    pump(1000);
    expect_text("信息"); expect_text("周末去湖边走走吗？");
    click_text("陈一帆"); expect_text("陈一帆");
    click_text(LV_SYMBOL_EDIT); expect_text("新信息");
    hachimi_desktop_home(); pump(420);

    CHECK(hachimi_desktop_launch(HACHIMI_APP_MAIL));
    pump(800);
    expect_text("信箱"); click_text("已加旗标"); expect_text("周末的湖边散步");
    click_text("收件箱"); click_text("Apple"); expect_text("收件人：我  ·  详情");
    hachimi_desktop_home(); pump(420);
    hachimi_desktop_destroy(); pump(40);
    puts("PASS Messages and Mail demo apps launch, navigate, compose, filter, read and return home");
    return 0;
}
