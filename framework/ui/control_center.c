#include "control_center.h"
#include "control_center_controls.h"
#include "lvgl.h"
#include "misc/cache/instance/lv_image_cache.h"

#define WIDTH 800
#define HEIGHT 480
#define DOWNSAMPLE 5
#define SMALL_W (WIDTH / DOWNSAMPLE)
#define SMALL_H (HEIGHT / DOWNSAMPLE)
#define SMALL_BYTES (SMALL_W * SMALL_H * 3)

static lv_obj_t * sheet;
static lv_obj_t * interaction_shield;
static lv_obj_t * backdrop;
static lv_obj_t * status_hit_area;
static lv_draw_buf_t * snapshot;
static bool (*open_allowed)(void);
static int32_t reveal;
static int32_t press_y;
static int32_t press_reveal;
static bool tracking;
static bool dragged;

static int clamp(int value, int maximum)
{
    return value < 0 ? 0 : value > maximum ? maximum : value;
}

/* Work on a 1/5-size RGB image. Three separable binomial Gaussian passes
 * soften the background without full-resolution convolution on Cortex-A7. */
static void gaussian_pass(const uint8_t * source, uint8_t * target, bool vertical)
{
    static const unsigned weights[] = {1, 4, 6, 4, 1};
    for(int y = 0; y < SMALL_H; y++) {
        for(int x = 0; x < SMALL_W; x++) {
            for(int channel = 0; channel < 3; channel++) {
                unsigned sum = 0;
                for(int k = -2; k <= 2; k++) {
                    int sx = vertical ? x : clamp(x + k, SMALL_W - 1);
                    int sy = vertical ? clamp(y + k, SMALL_H - 1) : y;
                    sum += source[(sy * SMALL_W + sx) * 3 + channel] * weights[k + 2];
                }
                target[(y * SMALL_W + x) * 3 + channel] = (uint8_t)((sum + 8) >> 4);
            }
        }
    }
}

static bool capture_background(void)
{
    static const uint8_t dither[4][4] = {
        {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}
    };
    lv_obj_t * screen = lv_screen_active();
    lv_obj_update_layout(screen);
    if(snapshot == NULL) snapshot = lv_snapshot_create_draw_buf(screen, LV_COLOR_FORMAT_RGB565);
    if(snapshot == NULL) return false;
    lv_image_cache_drop(snapshot);
    if(lv_snapshot_take_to_draw_buf(screen, LV_COLOR_FORMAT_RGB565, snapshot) != LV_RESULT_OK ||
       snapshot->header.w != WIDTH || snapshot->header.h != HEIGHT) return false;

    uint8_t * workspace = lv_malloc(SMALL_BYTES * 2);
    if(workspace == NULL) return false;
    uint8_t * small = workspace;
    uint8_t * temporary = workspace + SMALL_BYTES;
    unsigned stride = snapshot->header.stride / sizeof(uint16_t);
    uint16_t * pixels = (uint16_t *)snapshot->data;
    for(int y = 0; y < SMALL_H; y++) {
        for(int x = 0; x < SMALL_W; x++) {
            unsigned red = 0, green = 0, blue = 0;
            for(int dy = 0; dy < DOWNSAMPLE; dy++) {
                for(int dx = 0; dx < DOWNSAMPLE; dx++) {
                    unsigned p = pixels[(y * DOWNSAMPLE + dy) * stride + x * DOWNSAMPLE + dx];
                    red += (p >> 11) & 31;
                    green += (p >> 5) & 63;
                    blue += p & 31;
                }
            }
            unsigned offset = (y * SMALL_W + x) * 3;
            small[offset] = (uint8_t)(red * 255 / (25 * 31));
            small[offset + 1] = (uint8_t)(green * 255 / (25 * 63));
            small[offset + 2] = (uint8_t)(blue * 255 / (25 * 31));
        }
    }
    for(unsigned pass = 0; pass < 3; pass++) {
        gaussian_pass(small, temporary, false);
        gaussian_pass(temporary, small, true);
    }

    /* Expand and tint once into the original snapshot. Drag frames then draw
     * an ordinary opaque RGB565 image, with no scaling or blur work. */
    for(int y = 0; y < HEIGHT; y++) {
        int sy = y / DOWNSAMPLE, fy = y % DOWNSAMPLE;
        int next_y = clamp(sy + 1, SMALL_H - 1);
        for(int x = 0; x < WIDTH; x++) {
            int sx = x / DOWNSAMPLE, fx = x % DOWNSAMPLE;
            int next_x = clamp(sx + 1, SMALL_W - 1);
            unsigned rgb[3];
            for(int channel = 0; channel < 3; channel++) {
                unsigned a = small[(sy * SMALL_W + sx) * 3 + channel];
                unsigned b = small[(sy * SMALL_W + next_x) * 3 + channel];
                unsigned c = small[(next_y * SMALL_W + sx) * 3 + channel];
                unsigned d = small[(next_y * SMALL_W + next_x) * 3 + channel];
                rgb[channel] = ((a * (5 - fx) + b * fx) * (5 - fy) +
                                (c * (5 - fx) + d * fx) * fy + 12) / 25;
            }
            unsigned red = (rgb[0] * 3 + 20) / 4;
            unsigned green = (rgb[1] * 3 + 24) / 4;
            unsigned blue = (rgb[2] * 3 + 32) / 4;
            /* Ordered dithering keeps RGB565 gradients from forming bands.
             * It is baked into the cached image, with no animation cost. */
            unsigned threshold = dither[y & 3][x & 3];
            red = (unsigned)clamp((red + (threshold >> 1)) >> 3, 31);
            green = (unsigned)clamp((green + (threshold >> 2)) >> 2, 63);
            blue = (unsigned)clamp((blue + (threshold >> 1)) >> 3, 31);
            pixels[y * stride + x] = (uint16_t)((red << 11) | (green << 5) | blue);
        }
    }
    lv_free(workspace);
    lv_draw_buf_flush_cache(snapshot, NULL);
    lv_image_set_src(backdrop, snapshot);
    lv_obj_invalidate(backdrop);
    return true;
}

static void set_reveal(void * variable, int32_t value)
{
    (void)variable;
    reveal = clamp(value, HEIGHT);
    lv_obj_set_y(sheet, reveal - HEIGHT);
    /* Keep the blurred scene stationary as its visible area grows. */
    lv_obj_set_y(backdrop, HEIGHT - reveal);
    if(reveal == 0) {
        hachimi_control_center_controls_close();
        lv_obj_add_flag(sheet, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(interaction_shield, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_remove_flag(sheet, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(interaction_shield, LV_OBJ_FLAG_HIDDEN);
    }
}

static void settle(int32_t destination)
{
    lv_anim_delete(&reveal, set_reveal);
    if(reveal == destination) return;
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, &reveal);
    lv_anim_set_exec_cb(&animation, set_reveal);
    lv_anim_set_values(&animation, reveal, destination);
    lv_anim_set_duration(&animation, 200);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

static void drag_event(lv_event_t * event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if(code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
       code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) return;
    lv_indev_t * input = lv_event_get_indev(event);
    if(input == NULL) return;
    lv_point_t point;
    lv_indev_get_point(input, &point);
    if(code == LV_EVENT_PRESSED) {
        tracking = open_allowed == NULL || open_allowed();
        dragged = false;
        if(!tracking) return;
        lv_anim_delete(&reveal, set_reveal);
        press_y = point.y;
        press_reveal = reveal;
    }
    else if(code == LV_EVENT_PRESSING && tracking) {
        int delta = point.y - press_y;
        if(!dragged && (delta > 8 || delta < -8)) {
            dragged = true;
            if(press_reveal == 0) {
                if(delta < 0) { tracking = false; return; }
                if(capture_background()) lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_HIDDEN);
                else lv_obj_add_flag(backdrop, LV_OBJ_FLAG_HIDDEN);
                hachimi_control_center_controls_open();
            }
        }
        if(dragged) set_reveal(NULL, press_reveal + delta);
    }
    else if((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && tracking) {
        tracking = false;
        if(!dragged) { settle(press_reveal > HEIGHT / 2 ? HEIGHT : 0); return; }
        int delta = point.y - press_y;
        bool open = reveal > HEIGHT / 2;
        if(delta > 60) open = true;
        else if(delta < -60) open = false;
        settle(open ? HEIGHT : 0);
    }
}

static lv_obj_t * touch_surface(lv_obj_t * parent, int x, int y, int width, int height)
{
    lv_obj_t * object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(object, drag_event, LV_EVENT_ALL, NULL);
    return object;
}

void hachimi_control_center_create(bool (*can_open)(void))
{
    if(sheet != NULL) return;
    open_allowed = can_open;
    reveal = 0;
    tracking = dragged = false;
    /* Block underlying app touches even while the sheet is only partly open. */
    interaction_shield = touch_surface(lv_layer_top(), 0, 0, WIDTH, HEIGHT);
    lv_obj_add_flag(interaction_shield, LV_OBJ_FLAG_HIDDEN);
    sheet = touch_surface(lv_layer_top(), 0, -HEIGHT, WIDTH, HEIGHT);
    lv_obj_set_style_bg_color(sheet, lv_color_hex(0x252a32), 0);
    lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0);
    lv_obj_add_flag(sheet, LV_OBJ_FLAG_HIDDEN);
    backdrop = lv_image_create(sheet);
    lv_obj_set_size(backdrop, WIDTH, HEIGHT);
    lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(backdrop, LV_OBJ_FLAG_HIDDEN);
    hachimi_control_center_controls_create(sheet);
    lv_obj_t * handle = lv_obj_create(sheet);
    lv_obj_remove_style_all(handle);
    lv_obj_set_pos(handle, 358, 458);
    lv_obj_set_size(handle, 84, 5);
    lv_obj_set_style_radius(handle, 3, 0);
    lv_obj_set_style_bg_color(handle, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(handle, LV_OPA_60, 0);
    lv_obj_remove_flag(handle, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    status_hit_area = touch_surface(lv_layer_top(), 0, 0, WIDTH, 40);
}

bool hachimi_control_center_is_visible(void)
{
    return sheet != NULL && reveal > 0;
}

void hachimi_control_center_close(void)
{
    if(sheet == NULL) return;
    tracking = false;
    settle(0);
}

void hachimi_control_center_destroy(void)
{
    hachimi_control_center_controls_destroy();
    lv_anim_delete(&reveal, set_reveal);
    tracking = dragged = false;
    reveal = 0;
    if(status_hit_area != NULL) lv_obj_delete(status_hit_area);
    if(sheet != NULL) lv_obj_delete(sheet);
    if(interaction_shield != NULL) lv_obj_delete(interaction_shield);
    status_hit_area = sheet = backdrop = interaction_shield = NULL;
    if(snapshot != NULL) {
        lv_image_cache_drop(snapshot);
        lv_draw_buf_destroy(snapshot);
        snapshot = NULL;
    }
    open_allowed = NULL;
}
