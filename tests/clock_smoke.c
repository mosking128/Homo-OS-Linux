#define _POSIX_C_SOURCE 200809L
#include "framework/desktop.h"
#include "apps/clock/ios_clock.h"
#include "services/filesystem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(value) do { if(!(value)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #value); exit(1); } } while(0)
static uint16_t pixels[800 * 480];
static uint8_t draw_buffer[800 * 80 * 2];
static uint32_t simulated_tick;
static const char * artifacts;

static uint32_t ticks(void) { return simulated_tick; }
static void flush(lv_display_t * display, const lv_area_t * area, uint8_t * data)
{
    unsigned width = (unsigned)(area->x2 - area->x1 + 1);
    for(int y = area->y1; y <= area->y2; y++) {
        memcpy(pixels + y * 800 + area->x1, data + (y - area->y1) * width * 2, width * 2);
    }
    lv_display_flush_ready(display);
}
static void pump(unsigned milliseconds)
{
    for(unsigned i = 0; i < milliseconds; i += 5) {
        simulated_tick += 5;
        lv_timer_handler();
    }
}
static lv_obj_t * find_text(lv_obj_t * parent, const char * text)
{
    if(!lv_obj_is_visible(parent)) return NULL;
    if(lv_obj_check_type(parent, &lv_label_class) && !strcmp(lv_label_get_text(parent), text)) return parent;
    for(uint32_t i = 0; i < lv_obj_get_child_count(parent); i++) {
        lv_obj_t * found = find_text(lv_obj_get_child(parent, (int32_t)i), text);
        if(found != NULL) return found;
    }
    return NULL;
}
static lv_obj_t * find_widget(lv_obj_t * parent, const lv_obj_class_t * type, unsigned * index)
{
    if(lv_obj_check_type(parent, type) && (*index)-- == 0) return parent;
    for(uint32_t i = 0; i < lv_obj_get_child_count(parent); i++) {
        lv_obj_t * found = find_widget(lv_obj_get_child(parent, (int32_t)i), type, index);
        if(found != NULL) return found;
    }
    return NULL;
}
static lv_obj_t * widget(lv_obj_t * parent, const lv_obj_class_t * type, unsigned index)
{
    lv_obj_t * found = find_widget(parent, type, &index);
    CHECK(found != NULL);
    return found;
}
static void click_in(lv_obj_t * parent, const char * text)
{
    lv_obj_update_layout(parent);
    lv_obj_t * target = find_text(parent, text);
    if(target == NULL) fprintf(stderr, "Missing text: %s\n", text);
    CHECK(target != NULL);
    while(target != NULL && !lv_obj_has_flag(target, LV_OBJ_FLAG_CLICKABLE)) target = lv_obj_get_parent(target);
    CHECK(target != NULL);
    lv_obj_send_event(target, LV_EVENT_CLICKED, NULL);
    pump(50);
}
static void click(const char * text) { click_in(lv_screen_active(), text); }
static void launch(hachimi_app_id_t id) { CHECK(hachimi_desktop_launch(id)); pump(650); }
static void home(void) { CHECK(hachimi_desktop_home()); pump(450); }
static unsigned timer_count(void)
{
    unsigned count = 0;
    for(lv_timer_t * timer = lv_timer_get_next(NULL); timer != NULL; timer = lv_timer_get_next(timer)) count++;
    return count;
}
static void capture(const char * name)
{
    if(artifacts == NULL) return;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.ppm", artifacts, name);
    lv_refr_now(NULL);
    FILE * file = fopen(path, "wb"); CHECK(file != NULL);
    fprintf(file, "P6\n800 480\n255\n");
    for(unsigned i = 0; i < 800 * 480; i++) {
        uint16_t pixel = pixels[i];
        uint8_t rgb[3] = {(uint8_t)(((pixel >> 11) & 31) * 255 / 31),
                          (uint8_t)(((pixel >> 5) & 63) * 255 / 63),
                          (uint8_t)((pixel & 31) * 255 / 31)};
        CHECK(fwrite(rgb, 1, 3, file) == 3);
    }
    fclose(file);
}

int main(int argc, char ** argv)
{
    CHECK(argc >= 2); /* A disposable test HOME, never the real user directory. */
    CHECK(hachimi_fs_is_directory(argv[1]));
#ifdef _WIN32
    CHECK(_putenv_s("USERPROFILE", argv[1]) == 0);
#else
    CHECK(setenv("HOME", argv[1], 1) == 0);
#endif
    if(argc > 2) { artifacts = argv[2]; CHECK(hachimi_fs_is_directory(artifacts)); }
    char directory[1024], config[1024], seed[128];
    CHECK(hachimi_fs_join(argv[1], ".hachimi", directory, sizeof(directory)));
    CHECK(hachimi_fs_is_directory(directory) || hachimi_fs_mkdir(directory));
    CHECK(hachimi_fs_join(directory, "clock-alarms.txt", config, sizeof(config)));
    time_t now = time(NULL);
    struct tm current = *localtime(&now);
    snprintf(seed, sizeof(seed), "%d,%d,0,1,0\n", current.tm_hour, current.tm_min);
    CHECK(hachimi_fs_write_text(config, seed, !hachimi_fs_exists(config)));

    lv_init(); lv_tick_set_cb(ticks);
    lv_display_t * display = lv_display_create(800, 480);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, draw_buffer, NULL, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    unsigned baseline_timers = timer_count();
    ios_desktop_create(); pump(50);

    CHECK(find_text(lv_layer_top(), "停止") != NULL);
    unsigned desktop_overlays = lv_obj_get_child_count(lv_layer_top()) - 1;
    capture("alarm-at-startup");
    click_in(lv_layer_top(), "停止");
    CHECK(lv_obj_get_child_count(lv_layer_top()) == desktop_overlays);
    puts("PASS persisted alarm fires before Clock is ever opened");

    launch(HACHIMI_APP_CLOCK); capture("world-clock");
    CHECK(find_text(lv_screen_active(), "伦敦") != NULL);
    /* Regression: the sidebar's color must extend through the status bar. */
    CHECK(pixels[2 * 800 + 5] == pixels[42 * 800 + 5]);
    CHECK(pixels[2 * 800 + 5] != pixels[2 * 800 + 200]);
    click("闹钟"); click("+");
    /* The only visible rollers belong to the Clock editor. */
    lv_obj_t * hour = widget(lv_screen_active(), &lv_roller_class, 0);
    lv_obj_t * minute = widget(lv_screen_active(), &lv_roller_class, 1);
    unsigned saved_hour = (unsigned)(current.tm_hour + 3) % 24;
    lv_roller_set_selected(hour, saved_hour, LV_ANIM_OFF);
    lv_roller_set_selected(minute, 17, LV_ANIM_OFF);
    click("周一"); click("周三"); capture("alarm-editor"); click("存储");
    char saved_time[16]; snprintf(saved_time, sizeof(saved_time), "%02u:17", saved_hour);
    click(saved_time);
    CHECK(lv_roller_get_selected(widget(lv_screen_active(), &lv_roller_class, 0)) == saved_hour);
    minute = widget(lv_screen_active(), &lv_roller_class, 1);
    lv_roller_set_selected(minute, 18, LV_ANIM_OFF); click("存储");
    snprintf(saved_time, sizeof(saved_time), "%02u:18", saved_hour);
    CHECK(find_text(lv_screen_active(), saved_time) != NULL);
    char contents[1024], expected[64]; size_t length = 0;
    CHECK(hachimi_fs_read_text(config, contents, sizeof(contents), &length));
    snprintf(expected, sizeof(expected), "%u,18,10,1", saved_hour);
    CHECK(strstr(contents, expected) != NULL);
    capture("alarms");
    puts("PASS alarm add/edit, weekday repeat, one-shot disable and persistence");

    click("秒表"); click("开始"); pump(1250); click("计次"); pump(500); click("停止");
    CHECK(find_text(lv_screen_active(), "第1圈") != NULL);
    capture("stopwatch"); click("归零");
    CHECK(find_text(lv_screen_active(), "00:00.00") != NULL);
    puts("PASS stopwatch start, lap, pause and reset");

    click("计时器");
    lv_obj_t * minutes = widget(lv_screen_active(), &lv_roller_class, 1);
    lv_obj_t * seconds = widget(lv_screen_active(), &lv_roller_class, 2);
    lv_roller_set_selected(minutes, 0, LV_ANIM_OFF);
    lv_obj_send_event(minutes, LV_EVENT_VALUE_CHANGED, NULL);
    lv_roller_set_selected(seconds, 3, LV_ANIM_OFF);
    lv_obj_send_event(seconds, LV_EVENT_VALUE_CHANGED, NULL);
    capture("timer-picker");
    click("开始"); pump(500); click("暂停");
    CHECK(find_text(lv_screen_active(), "继续") != NULL);
    pump(4000); CHECK(lv_obj_get_child_count(lv_layer_top()) == desktop_overlays);
    capture("timer-paused"); click("继续"); home(); launch(HACHIMI_APP_CALCULATOR); pump(2000);
    CHECK(find_text(lv_screen_active(), "AC") != NULL);
    CHECK(find_text(lv_layer_top(), "计时结束") != NULL);
    capture("timer-over-calculator"); click_in(lv_layer_top(), "知道了");
    CHECK(lv_obj_get_child_count(lv_layer_top()) == desktop_overlays);
    puts("PASS timer pause/resume, background countdown and popup above another app");

    /* Reload a due alarm while Calculator is visible, then snooze it. */
    hachimi_clock_runtime_stop();
    now = time(NULL); current = *localtime(&now);
    CHECK(hachimi_fs_read_text(config, contents, sizeof(contents), &length));
    char due_config[1200];
    snprintf(due_config, sizeof(due_config), "%d,%d,0,1,0\n%d,%d,0,1,0\n%s",
             current.tm_hour, current.tm_min, current.tm_hour, current.tm_min, contents);
    CHECK(hachimi_fs_write_text(config, due_config, false));
    hachimi_clock_runtime_start(); pump(50);
    CHECK(find_text(lv_screen_active(), "AC") != NULL);
    CHECK(find_text(lv_layer_top(), "闹钟") != NULL);
    capture("alarm-over-calculator");
    click_in(lv_layer_top(), "稍后 9 分钟");
    CHECK(find_text(lv_layer_top(), "闹钟") != NULL);
    click_in(lv_layer_top(), "停止"); pump(1000);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == desktop_overlays);
    puts("PASS alarm popup above Calculator, snooze and simultaneous alert queue");

    hachimi_desktop_destroy(); pump(50);
    CHECK(timer_count() == baseline_timers);
    ios_desktop_create(); pump(50);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == desktop_overlays); /* consumed one-shot stays off */
    launch(HACHIMI_APP_CLOCK); click("闹钟");
    CHECK(find_text(lv_screen_active(), saved_time) != NULL);
    hachimi_desktop_destroy(); pump(50);
    CHECK(timer_count() == baseline_timers);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
    puts("PASS reload and repeated desktop shutdown release all Clock timers/overlays");
    return 0;
}
