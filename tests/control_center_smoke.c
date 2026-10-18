#include "framework/desktop.h"
#include "framework/ui/control_center.h"
#include "services/music.h"
#include "misc/cache/instance/lv_image_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(v) do { if(!(v)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #v); exit(1); } } while(0)
#define WIDTH 800
#define HEIGHT 480
static uint16_t pixels[WIDTH * HEIGHT], reference[WIDTH * HEIGHT], pattern[WIDTH * HEIGHT];
static uint8_t draw_buffer[WIDTH * 80 * 2];
static uint32_t tick;
static lv_point_t pointer_point;
static bool pointer_down;
static unsigned scene_clicks;
static void scene_clicked(lv_event_t * event) { (void)event; scene_clicks++; }
static uint32_t ticks(void) { return tick; }
static void flush(lv_display_t * display, const lv_area_t * area, uint8_t * data)
{
    int width = area->x2 - area->x1 + 1;
    for(int y = area->y1; y <= area->y2; y++)
        memcpy(pixels + y * WIDTH + area->x1, data + (y - area->y1) * width * 2, width * 2);
    lv_display_flush_ready(display);
}
static void read_pointer(lv_indev_t * device, lv_indev_data_t * data)
{
    (void)device; data->point = pointer_point;
    data->state = pointer_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
static void pump(unsigned ms)
{
    for(unsigned i = 0; i < ms; i += 5) { tick += 5; lv_timer_handler(); }
}
static void pointer(int x, int y, bool down)
{
    pointer_point = (lv_point_t){x, y}; pointer_down = down; pump(40);
}
static void pull_open(void)
{
    pointer(400, 12, true); pointer(400, 100, true); pointer(400, 330, true);
    pointer(400, 330, false); pump(250);
    CHECK(hachimi_control_center_is_visible());
}
static unsigned contrast(const uint16_t * image)
{
    unsigned sum = 0;
    for(int x = 41; x < 300; x++) {
        int a = (image[200 * WIDTH + x] >> 11) & 31;
        int b = (image[200 * WIDTH + x - 1] >> 11) & 31;
        sum += (unsigned)abs(a - b);
    }
    return sum;
}
static lv_obj_t * find_widget(lv_obj_t * parent, const lv_obj_class_t * type, unsigned * index)
{
    if(!lv_obj_is_visible(parent)) return NULL;
    if(lv_obj_check_type(parent,type) && (*index)--==0) return parent;
    for(unsigned i=0;i<lv_obj_get_child_count(parent);i++) {
        lv_obj_t * found=find_widget(lv_obj_get_child(parent,(int)i),type,index);
        if(found) return found;
    }
    return NULL;
}
static lv_obj_t * slider(unsigned index)
{
    lv_obj_t * found=find_widget(lv_layer_top(),&lv_slider_class,&index); CHECK(found); return found;
}
static bool has_text(lv_obj_t * parent, const char * text)
{
    if(!lv_obj_is_visible(parent)) return false;
    if(lv_obj_check_type(parent,&lv_label_class) && !strcmp(lv_label_get_text(parent),text)) return true;
    for(unsigned i=0;i<lv_obj_get_child_count(parent);i++)
        if(has_text(lv_obj_get_child(parent,(int)i),text)) return true;
    return false;
}
static void tap(int x,int y) { pointer(x,y,true); pointer(x,y,false); }
static void controls_test(void)
{
    pointer(605,360,true); pointer(605,220,true); pointer(605,220,false);
    CHECK(lv_slider_get_value(slider(0))>=80); CHECK(hachimi_control_center_is_visible());
    pointer(605,220,true); pointer(605,397,true); pointer(605,397,false);
    CHECK(lv_slider_get_value(slider(0))==20); CHECK(!has_text(lv_layer_top(),"亮度 20%"));
    pointer(717,350,true); pointer(717,220,true); pointer(717,220,false);
    hachimi_music_state_t state; hachimi_music_state(&state); CHECK(state.volume>=80);
    pointer(717,220,true); pointer(717,397,true); pointer(717,397,false);
    hachimi_music_state(&state); CHECK(state.volume<=1); CHECK(hachimi_control_center_is_visible());
    hachimi_music_volume(37); pump(550); CHECK(lv_slider_get_value(slider(1))==37);
    tap(430,130); CHECK(has_text(lv_layer_top(),"预览 · 已关闭")); tap(430,130);
    tap(650,130); CHECK(has_text(lv_layer_top(),"预览 · 已关闭")); tap(650,130);
    CHECK(has_text(lv_layer_top(),"以太网"));
    if(state.count>=2) {
        tap(437,360);
#ifdef _WIN32
        hachimi_music_state(&state); CHECK(!state.playing && strstr(state.error,"Linux"));
#endif
        tap(503,360); hachimi_music_state(&state); CHECK(state.index==1);
        CHECK(has_text(lv_layer_top(),state.playing ? hachimi_music_track(1)->title : "暂无播放"));
        tap(371,360); hachimi_music_state(&state); CHECK(state.index==0);
        CHECK(has_text(lv_layer_top(),state.playing ? hachimi_music_track(0)->title : "暂无播放"));
    }
    pointer(605,300,true); pointer(605,246,true); pointer(605,246,false);
    hachimi_music_volume(65); pump(550);
    CHECK(hachimi_control_center_is_visible()); CHECK(scene_clicks==0);
    puts("PASS brightness floor, volume backend and external sync, network previews and player routing without dismissing the sheet");
}
static void save_image(const char * path)
{
    if(path == NULL) return;
    FILE * file = fopen(path, "wb"); CHECK(file);
    fprintf(file, "P6\n800 480\n255\n");
    for(unsigned i = 0; i < WIDTH * HEIGHT; i++) {
        unsigned p = pixels[i];
        uint8_t rgb[] = {((p >> 11) & 31) * 255 / 31, ((p >> 5) & 63) * 255 / 63, (p & 31) * 255 / 31};
        CHECK(fwrite(rgb, 1, 3, file) == 3);
    }
    CHECK(fclose(file) == 0);
}
int main(int argc, char ** argv)
{
    if(argc>3) {
#ifdef _WIN32
        CHECK(_putenv_s("HACHIMI_MUSIC_DIR",argv[3])==0);
#else
        CHECK(setenv("HACHIMI_MUSIC_DIR",argv[3],1)==0);
#endif
    }
    lv_init(); lv_tick_set_cb(ticks);
    lv_display_t * display = lv_display_create(WIDTH, HEIGHT);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, draw_buffer, NULL, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    lv_indev_t * input = lv_indev_create(); lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(input, read_pointer); lv_indev_set_display(input, display);
    ios_desktop_create(); pump(50);
    if(argc>3) { hachimi_music_state_t music; hachimi_music_state(&music); CHECK(music.count==2); }
    for(unsigned y = 0; y < HEIGHT; y++) for(unsigned x = 0; x < WIDTH; x++)
        pattern[y * WIDTH + x] = (x / 5) % 2 ? 0xffff : 0;
    lv_image_dsc_t image = {.header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
        .w = WIDTH, .h = HEIGHT, .stride = WIDTH * 2}, .data_size = sizeof(pattern), .data = (uint8_t *)pattern};
    lv_obj_t * scene = lv_image_create(lv_screen_active()); lv_image_set_src(scene, &image);
    lv_obj_add_flag(scene, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scene, scene_clicked, LV_EVENT_CLICKED, NULL); pump(50);
    memcpy(reference, pixels, sizeof(pixels)); CHECK(contrast(reference) > 1000);

    pointer(400, 12, true); pointer(400, 30, true); pointer(400, 30, false); pump(250);
    CHECK(!hachimi_control_center_is_visible());
    pointer(400, 12, true); pointer(400, 100, true); pointer(400, 100, false);
    CHECK(hachimi_control_center_is_visible());
    pointer(400, 470, true); pointer(400, 470, false); CHECK(scene_clicks == 0);
    hachimi_control_center_close(); pump(250);
    pull_open(); CHECK(contrast(pixels) < contrast(reference) / 5);
    CHECK(pixels[470 * WIDTH + 400] != reference[470 * WIDTH + 400]);
    CHECK(!hachimi_desktop_launch(HACHIMI_APP_CALCULATOR));
    uint16_t blurred = pixels[200 * WIDTH + 400];
    controls_test();
    pointer(400, 200, true); pointer(400, 200, false); pump(250);
    CHECK(hachimi_control_center_is_visible()); CHECK(pixels[200 * WIDTH + 400] == blurred);
    pointer(400, 400, true); pointer(400, 100, true); pointer(400, 100, false); pump(250);
    CHECK(!hachimi_control_center_is_visible()); CHECK(!memcmp(reference, pixels, sizeof(pixels)));
    CHECK(scene_clicks == 0);
    puts("PASS pointer pull/release, full-screen Gaussian blur, tap stays open, swipe-up dismiss and scene restoration");

    for(unsigned i = 0; i < WIDTH * HEIGHT; i++) pattern[i] = 0xf800;
    lv_image_cache_drop(&image); lv_obj_invalidate(scene); pump(50);
    pull_open(); CHECK(pixels[200 * WIDTH + 400] != blurred);
    CHECK(hachimi_desktop_home()); pump(250); CHECK(!hachimi_control_center_is_visible());
    lv_obj_delete(scene);
    CHECK(hachimi_desktop_launch(HACHIMI_APP_CALCULATOR)); pump(500);
    pull_open(); save_image(argc > 1 ? argv[1] : NULL);
    CHECK(hachimi_desktop_home()); pump(250); CHECK(!hachimi_control_center_is_visible());
    CHECK(hachimi_desktop_home()); pump(500);
    puts("PASS background recapture, app status-bar pull and Home dismisses control center before the app");

    pull_open(); save_image(argc > 2 ? argv[2] : NULL);
    hachimi_desktop_destroy(); pump(250);
    CHECK(!hachimi_control_center_is_visible()); CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
    ios_desktop_create(); pump(50);
    pointer(400, 12, true); pointer(400, 100, true); pointer(400, 100, false);
    hachimi_desktop_destroy(); pump(250);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
    lv_indev_delete(input);
    puts("PASS repeated destruction during open/settle releases overlay and cached image");
    return 0;
}
