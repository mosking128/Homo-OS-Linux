#include "framework/desktop.h"
#include "framework/ui/fonts.h"
#include "framework/ui/ios_layout.h"
#include "services/filesystem.h"
#include "services/clock.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CHECK(condition) do { if(!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while(0)
static uint16_t pixels[800 * 480];
static uint8_t draw_buffer[800 * 80 * 2] __attribute__((aligned(64)));
static const char * artifact_dir;
static lv_indev_data_t touch;

static void read_touch(lv_indev_t * indev, lv_indev_data_t * data)
{
    (void)indev;
    data->point = touch.point;
    data->state = touch.state;
}

static uint32_t ticks(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32_t)((uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000);
}
static void flush(lv_display_t * display, const lv_area_t * area, uint8_t * data)
{
    int width = area->x2 - area->x1 + 1;
    for(int y = area->y1; y <= area->y2; y++)
        memcpy(pixels + y * 800 + area->x1, data + (y - area->y1) * width * 2, width * 2);
    lv_display_flush_ready(display);
}
static void pump(unsigned duration)
{
    uint32_t start = ticks();
    do { lv_timer_handler(); usleep(3000); } while((uint32_t)(ticks() - start) < duration);
}
static lv_obj_t * find_text(lv_obj_t * parent, const char * text, bool result)
{
    if(!lv_obj_is_visible(parent)) return NULL;
    if(lv_obj_check_type(parent, &lv_label_class) && !strcmp(lv_label_get_text(parent), text) &&
       (!result || !lv_obj_check_type(lv_obj_get_parent(parent), &lv_button_class))) return parent;
    for(uint32_t i = 0; i < lv_obj_get_child_count(parent); i++) {
        lv_obj_t * found = find_text(lv_obj_get_child(parent, i), text, result);
        if(found) return found;
    }
    return NULL;
}
static lv_obj_t * text(const char * value)
{
    lv_obj_update_layout(lv_screen_active());
    lv_obj_t * obj = find_text(lv_screen_active(), value, false);
    if(obj == NULL) fprintf(stderr, "Missing visible text: %s\n", value);
    CHECK(obj);
    return obj;
}
static lv_obj_t * nth_slider(lv_obj_t * parent, unsigned * index)
{
    if(!lv_obj_is_visible(parent)) return NULL;
    if(lv_obj_check_type(parent, &lv_slider_class)) {
        if((*index)-- == 0) return parent;
    }
    for(uint32_t i = 0; i < lv_obj_get_child_count(parent); i++) {
        lv_obj_t * found = nth_slider(lv_obj_get_child(parent, i), index);
        if(found != NULL) return found;
    }
    return NULL;
}
static lv_obj_t * click(const char * value)
{
    lv_obj_t * target = text(value);
    while(!lv_obj_has_flag(target, LV_OBJ_FLAG_CLICKABLE)) target = lv_obj_get_parent(target);
    CHECK(target);
    lv_obj_send_event(target, LV_EVENT_CLICKED, NULL);
    pump(60);
    return target;
}
static lv_obj_t * textarea(lv_obj_t * panel, unsigned index)
{
    for(uint32_t i = 0; i < lv_obj_get_child_count(panel); i++) {
        lv_obj_t * child = lv_obj_get_child(panel, i);
        if(lv_obj_check_type(child, &lv_textarea_class)) { if(index-- == 0) return child; }
    }
    CHECK(false); return NULL;
}
static void capture(const char * name)
{
    if(artifact_dir == NULL) return;
    lv_refr_now(NULL);
    char path[1024]; snprintf(path, sizeof(path), "%s/%s.rgb565", artifact_dir, name);
    FILE * file = fopen(path, "wb"); CHECK(file);
    CHECK(fwrite(pixels, sizeof(pixels), 1, file) == 1);
    fclose(file);
}
static void home(void) { CHECK(hachimi_desktop_home()); pump(450); }
static void launch(hachimi_app_id_t id)
{
    if(!hachimi_desktop_launch(id)) fprintf(stderr, "Cannot launch app id %d\n", (int)id);
    else { pump(650); return; }
    CHECK(false);
}
static void swipe(int from_x, int from_y, int to_x, int to_y)
{
    touch.point.x = from_x; touch.point.y = from_y;
    touch.state = LV_INDEV_STATE_PRESSED; pump(80);
    /* Sample faster than LVGL's input timer; stationary samples reset the
     * legacy gesture accumulator, so sparse synthetic moves are not a swipe. */
    for(int step = 1; step <= 24; step++) {
        touch.point.x = from_x + (to_x - from_x) * step / 24;
        touch.point.y = from_y + (to_y - from_y) * step / 24;
        pump(6);
    }
    touch.state = LV_INDEV_STATE_RELEASED; pump(500);
}
static unsigned timers(void)
{
    unsigned count = 0;
    for(lv_timer_t * timer = lv_timer_get_next(NULL); timer; timer = lv_timer_get_next(timer)) count++;
    return count;
}

int main(int argc, char ** argv)
{
    char test_home[] = "/tmp/hachimi-ui-smoke-XXXXXX";
    CHECK(mkdtemp(test_home)); CHECK(setenv("HOME", test_home, 1) == 0);
    if(argc > 1) { artifact_dir = argv[1]; CHECK(hachimi_fs_is_directory(artifact_dir) || hachimi_fs_mkdir(artifact_dir)); }
    lv_init(); lv_tick_set_cb(ticks);
    lv_display_t * display = lv_display_create(800, 480);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, draw_buffer, NULL, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    lv_indev_t * input = lv_indev_create();
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(input, read_touch);
    lv_indev_set_display(input, display);
    unsigned baseline_timers = timers();
    ios_desktop_create(); pump(100);
    capture("desktop");

    launch(HACHIMI_APP_SETTINGS);
    text("关于设备"); click("网络与连接"); pump(1800); text("其他连接"); capture("settings-network");
    click("时间与日期"); pump(1800); text("联网校对时间");
    CHECK(find_text(lv_screen_active(), "无法读取系统时间服务", false) == NULL);
    CHECK(find_text(lv_screen_active(), "正在读取日期…", false) == NULL);
    capture("settings-time");
    click("显示与亮度"); text("屏幕亮度"); capture("settings-display"); home();
    puts("PASS settings: navigation, network/time results, display page");

    launch(HACHIMI_APP_FILES);
    lv_obj_t * create_label = text("新建文件夹"); lv_area_t bounds; lv_obj_get_coords(create_label, &bounds);
    CHECK(bounds.y1 >= IOS_APP_CONTENT_SAFE_TOP);
    click("新建文件夹");
    lv_obj_t * panel = lv_obj_get_parent(lv_obj_get_parent(text("创建")));
    lv_textarea_set_text(textarea(panel, 0), "smoke-folder"); click("创建"); text("smoke-folder");
    click("+ 文件"); panel = lv_obj_get_parent(lv_obj_get_parent(text("创建")));
    lv_textarea_set_text(textarea(panel, 0), "smoke-note.txt");
    lv_textarea_set_text(textarea(panel, 1), "first content"); click("创建");
    click("smoke-note.txt"); panel = lv_obj_get_parent(lv_obj_get_parent(text("存储")));
    lv_textarea_set_text(textarea(panel, 0), "renamed-note.txt");
    lv_textarea_set_text(textarea(panel, 1), "edited content"); click("存储");
    char path[1024], content[128]; size_t length;
    CHECK(hachimi_fs_join(test_home, "renamed-note.txt", path, sizeof(path)));
    CHECK(hachimi_fs_read_text(path, content, sizeof(content), &length) && !strcmp(content, "edited content"));
    lv_obj_t * row = lv_obj_get_parent(text("renamed-note.txt"));
    lv_obj_send_event(lv_obj_get_child(row, -1), LV_EVENT_CLICKED, NULL); click("删除"); click("删除");
    CHECK(!hachimi_fs_exists(path));
    capture("files"); home();
    puts("PASS Files UI: safe area, new folder/file, edit/rename, delete");

    launch(HACHIMI_APP_CALCULATOR);
    click("1"); click("+"); click("2"); click("=");
    CHECK(find_text(lv_screen_active(), "3", true));
    click("C"); click("AC"); click("SCI"); click("3"); click("0"); click("sin");
    CHECK(find_text(lv_screen_active(), "0.5", true));
    char clock[8]; CHECK(hachimi_clock_format(clock, sizeof(clock), "%H:%M"));
    CHECK(lv_obj_get_style_text_font(text(clock), LV_PART_MAIN) == hachimi_ui_font(18));
    capture("calculator-scientific"); home();
    puts("PASS calculator: 1+2=3, sin(30)=0.5, real clock and shared 18px font");

    launch(HACHIMI_APP_LED_CONTROL);
    text("红色"); text("绿色"); text("蓝色"); text("颜色预览");
    lv_obj_t * rgb_sliders[3]; int previous_rgb[3];
    for(unsigned i = 0; i < 3; i++) {
        unsigned slider_index = i;
        rgb_sliders[i] = nth_slider(lv_screen_active(), &slider_index); CHECK(rgb_sliders[i]);
        previous_rgb[i] = lv_slider_get_value(rgb_sliders[i]);
        lv_slider_set_value(rgb_sliders[i], 0, LV_ANIM_OFF);
        lv_obj_send_event(rgb_sliders[i], LV_EVENT_VALUE_CHANGED, NULL);
    }
    lv_obj_t * red_slider = rgb_sliders[0];
    lv_slider_set_value(red_slider, 255, LV_ANIM_OFF);
    lv_obj_send_event(red_slider, LV_EVENT_VALUE_CHANGED, NULL); pump(80);
    text("#FF0000"); capture("led-control-red-preview");
    for(unsigned i = 0; i < 3; i++) {
        lv_slider_set_value(rgb_sliders[i], previous_rgb[i], LV_ANIM_OFF);
        lv_obj_send_event(rgb_sliders[i], LV_EVENT_VALUE_CHANGED, NULL);
    }
    pump(100);
    home();
    puts("PASS LED Control: RGB sliders render and update the mixed-color preview");

    launch(HACHIMI_APP_CALCULATOR); swipe(400, 469, 400, 300);
    launch(HACHIMI_APP_FILES); swipe(12, 240, 230, 240);
    launch(HACHIMI_APP_SETTINGS); swipe(788, 240, 570, 240);
    launch(HACHIMI_APP_CALCULATOR); home();
    puts("PASS pointer gestures: bottom, left and right edge return home");

    for(int i = 0; i < 3; i++) { launch(HACHIMI_APP_SETTINGS); home(); launch(HACHIMI_APP_FILES); home(); launch(HACHIMI_APP_CALCULATOR); home(); }
    hachimi_desktop_destroy(); pump(80);
    CHECK(timers() == baseline_timers);
    ios_desktop_create(); launch(HACHIMI_APP_SETTINGS); home(); hachimi_desktop_destroy(); pump(80);
    CHECK(timers() == baseline_timers);
    CHECK(hachimi_fs_remove(test_home));
    puts("PASS lifecycle: repeated launches, destroy/recreate, no leaked app timers");
    return 0;
}
