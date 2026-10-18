#include "desktop.h"
#include "ui/ios_layout.h"
#include "ui/ios_status_bar.h"
#include "ui/control_center.h"
#include "app_registry.h"
#include "app_manager.h"

#include "lvgl.h"
#include "misc/cache/instance/lv_image_cache.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define IOS_ASSET(name) "A:assets/ios/" name

#define DISPLAY_WIDTH 800
#define DISPLAY_HEIGHT 480
#define ICON_SIZE 64
#define MAX_APP_LAUNCHES 24
#define EDGE_GESTURE_BOTTOM_SIZE 36
#ifdef HACHIMI_SIMULATOR
#define EDGE_GESTURE_SIDE_SIZE 64
#else
#define EDGE_GESTURE_SIDE_SIZE EDGE_GESTURE_BOTTOM_SIZE
#endif
#define APP_OPEN_DURATION 320
#define APP_CLOSE_DURATION 250
#define APP_CORNER_RADIUS 20
#define ANIMATION_SCALE 1024

typedef struct {
    const hachimi_app_descriptor_t * app;
    lv_coord_t x;
    lv_coord_t y;
} app_launch_t;

typedef enum {
    APP_STATE_IDLE,
    APP_STATE_OPENING,
    APP_STATE_OPEN,
    APP_STATE_CLOSING,
} app_state_t;

typedef enum {
    GESTURE_FROM_LEFT,
    GESTURE_FROM_RIGHT,
    GESTURE_FROM_BOTTOM,
} app_gesture_action_t;


typedef struct {
    int32_t from_x;
    int32_t from_y;
    int32_t from_width;
    int32_t from_height;
    int32_t from_radius;
    int32_t to_x;
    int32_t to_y;
    int32_t to_width;
    int32_t to_height;
    int32_t to_radius;
} app_transition_t;

static hachimi_app_manager_t app_manager;
static lv_obj_t * app_view;
static lv_obj_t * app_mask;
static lv_obj_t * transition_content;
static lv_draw_buf_t * transition_content_buffer;
static lv_obj_t * transition_background;
static lv_draw_buf_t * transition_background_buffer;
static lv_obj_t * app_title;
static lv_obj_t * home_indicator;
static lv_obj_t * gesture_zones[3];
static app_launch_t app_launches[MAX_APP_LAUNCHES];
static size_t app_launch_count;
static app_launch_t * selected_launch;
static app_state_t app_state = APP_STATE_IDLE;
static app_transition_t app_transition;
static hachimi_desktop_transition_backend_t transition_backend;
static bool direct_transition_active;

/* Capture once, before hiding the application. Never relayout live widgets
 * during the animation, and retain the allocation for the next close. */
static const lv_draw_buf_t * cache_transition_content(void)
{
    lv_obj_add_flag(transition_content, LV_OBJ_FLAG_HIDDEN);
    const char * enabled = getenv("HACHIMI_TRANSITION_CONTENT");
    if(enabled && strcmp(enabled, "0") == 0) return NULL;
    lv_obj_update_layout(app_view);
    if(transition_content_buffer == NULL)
        transition_content_buffer = lv_snapshot_create_draw_buf(app_view, LV_COLOR_FORMAT_RGB565);
    if(transition_content_buffer == NULL) return NULL;
    lv_image_cache_drop(transition_content_buffer);
    if(lv_snapshot_take_to_draw_buf(app_view, LV_COLOR_FORMAT_RGB565,
                                   transition_content_buffer) != LV_RESULT_OK) return NULL;
    lv_image_set_src(transition_content, transition_content_buffer);
    lv_obj_clear_flag(transition_content, LV_OBJ_FLAG_HIDDEN);
    return transition_content_buffer;
}

/* Cubic deceleration: immediate response, zero terminal velocity, no overshoot.
 * Integer arithmetic keeps the motion identical on simulator and Cortex-A7. */
static int32_t app_transition_path(const lv_anim_t * anim)
{
    if(anim->duration <= 0) return anim->end_value;
    int32_t t = anim->act_time < 0 ? 0 : anim->act_time;
    if(t > anim->duration) t = anim->duration;
    int32_t remaining = ANIMATION_SCALE - t * ANIMATION_SCALE / anim->duration;
    int32_t eased = ANIMATION_SCALE - (int32_t)((int64_t)remaining * remaining * remaining /
                                                (ANIMATION_SCALE * ANIMATION_SCALE));
    return anim->start_value + (int32_t)((int64_t)(anim->end_value - anim->start_value) * eased / ANIMATION_SCALE);
}

static bool cache_transition_background(void)
{
    lv_obj_add_flag(transition_background, LV_OBJ_FLAG_HIDDEN);
    const char * enabled = getenv("HACHIMI_TRANSITION_CACHE");
    if(enabled && strcmp(enabled, "0") == 0) return false;
    lv_obj_t * screen = lv_screen_active();
    if(transition_background_buffer == NULL) {
        transition_background_buffer = lv_snapshot_create_draw_buf(screen, LV_COLOR_FORMAT_RGB565);
    }
    if(transition_background_buffer == NULL) return false;
    lv_image_cache_drop(transition_background_buffer);
    if(lv_snapshot_take_to_draw_buf(screen, LV_COLOR_FORMAT_RGB565, transition_background_buffer) != LV_RESULT_OK) return false;
    lv_image_set_src(transition_background, transition_background_buffer);
    lv_obj_clear_flag(transition_background, LV_OBJ_FLAG_HIDDEN);
    return true;
}



static const app_gesture_action_t gesture_actions[] = {
    GESTURE_FROM_LEFT,
    GESTURE_FROM_RIGHT,
    GESTURE_FROM_BOTTOM,
};

static int32_t clamp_i32(int32_t value, int32_t min_value, int32_t max_value)
{
    if(value < min_value) return min_value;
    if(value > max_value) return max_value;
    return value;
}

static int32_t interpolate_i32(int32_t from, int32_t to, int32_t progress)
{
    return from + (int32_t)(((int64_t)(to - from) * progress) / ANIMATION_SCALE);
}

static void layout_app_content(void)
{
    if(app_view == NULL) return;

    lv_coord_t parent_width = lv_obj_get_width(app_view);
    lv_coord_t parent_height = lv_obj_get_height(app_view);
    if(app_title != NULL) {
        lv_obj_set_pos(app_title,
                       (parent_width - lv_obj_get_width(app_title)) / 2,
                       (parent_height - lv_obj_get_height(app_title)) / 2);
    }
    hachimi_app_manager_layout(&app_manager, parent_width, parent_height);
    if(home_indicator != NULL) {
        lv_obj_set_pos(home_indicator,
                       (parent_width - lv_obj_get_width(home_indicator)) / 2,
                       parent_height - lv_obj_get_height(home_indicator) - 12);
    }
}

static void app_transition_step(void * var, int32_t progress)
{
    app_transition_t * transition = var;
    if(transition == NULL || app_mask == NULL) return;

    int32_t x = interpolate_i32(transition->from_x, transition->to_x, progress);
    int32_t y = interpolate_i32(transition->from_y, transition->to_y, progress);
    int32_t width = interpolate_i32(transition->from_width, transition->to_width, progress);
    int32_t height = interpolate_i32(transition->from_height, transition->to_height, progress);
    /* Keep the card rounded until it meets the display edges. On return,
     * establish the corners early instead of sharpening a shrinking card. */
    int32_t edge_distance = app_state == APP_STATE_OPENING ? ANIMATION_SCALE - progress : progress;
    int32_t radius = clamp_i32(edge_distance * APP_CORNER_RADIUS / 160, 0, APP_CORNER_RADIUS);
    if(direct_transition_active && transition_backend.draw != NULL) {
        transition_backend.draw(transition_backend.user_data, x, y,
                               clamp_i32(width, 1, DISPLAY_WIDTH + 80),
                               clamp_i32(height, 1, DISPLAY_HEIGHT + 80),
                               clamp_i32(radius, 0, APP_CORNER_RADIUS),
                               app_state == APP_STATE_CLOSING && progress >= ANIMATION_SCALE);
        return; /* No invisible LVGL transforms/layout during framebuffer animation. */
    }
    lv_obj_set_pos(app_mask, (lv_coord_t)x, (lv_coord_t)y);
    lv_obj_set_size(app_mask, (lv_coord_t)clamp_i32(width, 1, DISPLAY_WIDTH + 80),
                    (lv_coord_t)clamp_i32(height, 1, DISPLAY_HEIGHT + 80));
    lv_obj_set_style_radius(app_mask, clamp_i32(radius, 0, APP_CORNER_RADIUS), LV_PART_MAIN);
}

static void finish_direct_transition(void)
{
    if(!direct_transition_active) return;
    direct_transition_active = false;
    if(transition_backend.end != NULL) transition_backend.end(transition_backend.user_data);
}

static void present_direct_transition_target(void)
{
    if(transition_backend.present != NULL) transition_backend.present(transition_backend.user_data);
}

/* Called from an animation completion callback. lv_refr_now() also runs the
 * animation engine, which reenters its linked-list traversal and can leave
 * a freed animation node in use when other app animations are alive. */
static void refresh_transition_target(void)
{
    lv_display_t * display=lv_display_get_next(NULL);
    while(display) {
        lv_timer_t * timer=lv_display_get_refr_timer(display);
        if(timer) lv_display_refr_timer(timer);
        display=lv_display_get_next(display);
    }
}

static void app_transition_complete(lv_anim_t * anim)
{
    LV_UNUSED(anim);

    if(app_state == APP_STATE_OPENING) {
        bool direct = direct_transition_active;
        /* Present the full-screen white mask before creating or opening the app. */
        if(!direct) {
            app_transition_step(&app_transition, ANIMATION_SCALE);
            lv_obj_clear_flag(app_mask, LV_OBJ_FLAG_HIDDEN);
        }
        finish_direct_transition();
        if(!direct) refresh_transition_target();
        if(!hachimi_app_manager_open(&app_manager, selected_launch->app->module)) {
            lv_obj_add_flag(app_mask, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(transition_background, LV_OBJ_FLAG_HIDDEN);
            selected_launch = NULL;
            app_state = APP_STATE_IDLE;
            if(direct) lv_obj_invalidate(lv_screen_active());
            refresh_transition_target();
            if(direct) present_direct_transition_target();
            return;
        }
        lv_obj_clear_flag(app_view, LV_OBJ_FLAG_HIDDEN);
        layout_app_content();
        lv_obj_update_layout(app_view);
        lv_obj_add_flag(app_mask, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(transition_background, LV_OBJ_FLAG_HIDDEN);
        app_state = APP_STATE_OPEN;
        for(size_t i = 0; i < sizeof(gesture_zones) / sizeof(gesture_zones[0]); i++) {
            /* CarPlay's narrow left rail contains navigation controls. Keep
             * its full hit area usable; bottom/right return gestures remain. */
            if(i == 0 && selected_launch->app->reserve_left_edge) {
                lv_obj_add_flag(gesture_zones[i], LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            lv_obj_clear_flag(gesture_zones[i], LV_OBJ_FLAG_HIDDEN);
        }
        if(direct) {
            lv_obj_invalidate(lv_screen_active());
            refresh_transition_target();
            present_direct_transition_target();
        }
    }
    else if(app_state == APP_STATE_CLOSING) {
        bool direct = direct_transition_active;
        for(size_t i = 0; i < sizeof(gesture_zones) / sizeof(gesture_zones[0]); i++) {
            lv_obj_add_flag(gesture_zones[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(app_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(app_mask, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(transition_background, LV_OBJ_FLAG_HIDDEN);
        hachimi_app_manager_close(&app_manager);
        app_state = APP_STATE_IDLE;
        finish_direct_transition();
        if(direct) lv_obj_invalidate(lv_screen_active());
        refresh_transition_target();
        if(direct) present_direct_transition_target();
    }
}

static void start_app_transition(bool opening)
{
    if(selected_launch == NULL || app_view == NULL || app_mask == NULL) return;

    const lv_draw_buf_t * content = NULL;
    if(!opening) content = cache_transition_content();
    else lv_obj_add_flag(transition_content, LV_OBJ_FLAG_HIDDEN);
    app_state = opening ? APP_STATE_OPENING : APP_STATE_CLOSING;
    if(opening) {
        app_transition.from_x = selected_launch->x;
        app_transition.from_y = selected_launch->y;
        app_transition.from_width = ICON_SIZE;
        app_transition.from_height = ICON_SIZE;
        app_transition.from_radius = APP_CORNER_RADIUS;
        app_transition.to_x = 0;
        app_transition.to_y = 0;
        app_transition.to_width = DISPLAY_WIDTH;
        app_transition.to_height = DISPLAY_HEIGHT;
        app_transition.to_radius = 0;
    }
    else {
        app_transition.from_x = 0;
        app_transition.from_y = 0;
        app_transition.from_width = DISPLAY_WIDTH;
        app_transition.from_height = DISPLAY_HEIGHT;
        app_transition.from_radius = 0;
        app_transition.to_x = selected_launch->x;
        app_transition.to_y = selected_launch->y;
        app_transition.to_width = ICON_SIZE;
        app_transition.to_height = ICON_SIZE;
        app_transition.to_radius = APP_CORNER_RADIUS;
    }

    /* Animate a frozen card; the app keeps its full-screen layout. A cold
     * launch retains the cheap opaque panel so app construction cannot delay
     * the first visible response. Failed snapshots use that same fallback. */
    lv_obj_add_flag(app_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(app_mask, LV_OBJ_FLAG_HIDDEN);
    for(size_t i = 0; i < sizeof(gesture_zones) / sizeof(gesture_zones[0]); i++) {
        lv_obj_add_flag(gesture_zones[i], LV_OBJ_FLAG_HIDDEN);
    }
    bool snapshot_ready = cache_transition_background();
    direct_transition_active = snapshot_ready && transition_backend.begin != NULL &&
        transition_backend.begin(transition_backend.user_data, transition_background_buffer, content,
                                 app_transition.from_x, app_transition.from_y,
                                 app_transition.from_width, app_transition.from_height,
                                 app_transition.from_radius);
    if(direct_transition_active) lv_obj_add_flag(transition_background, LV_OBJ_FLAG_HIDDEN);
    else {
        app_transition_step(&app_transition, 0);
        lv_obj_clear_flag(app_mask, LV_OBJ_FLAG_HIDDEN);
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, &app_transition);
    lv_anim_set_exec_cb(&anim, app_transition_step);
    lv_anim_set_values(&anim, 0, ANIMATION_SCALE);
    /* Both rendering paths already presented progress 0 above. */
    lv_anim_set_early_apply(&anim, false);
    lv_anim_set_duration(&anim, opening ? APP_OPEN_DURATION : APP_CLOSE_DURATION);
    lv_anim_set_path_cb(&anim, app_transition_path);
    lv_anim_set_completed_cb(&anim, app_transition_complete);
    lv_anim_start(&anim);
}

static void close_app(void)
{
    if(hachimi_control_center_is_visible()) return;
    if(app_state != APP_STATE_OPEN) return;
    start_app_transition(false);
}

void hachimi_desktop_set_app_gestures_enabled(bool enabled)
{
    if(app_state != APP_STATE_OPEN) return;

    for(size_t i = 0; i < sizeof(gesture_zones) / sizeof(gesture_zones[0]); i++) {
        if(!enabled || (i == 0 && selected_launch != NULL &&
                        selected_launch->app->reserve_left_edge)) {
            lv_obj_add_flag(gesture_zones[i], LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_clear_flag(gesture_zones[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static bool launch_app(app_launch_t * launch)
{
    if(hachimi_control_center_is_visible()) return false;
    if(launch == NULL || launch->app == NULL || app_view == NULL ||
       app_title == NULL || app_state != APP_STATE_IDLE) return false;

    selected_launch = launch;
    lv_image_set_src(app_title, launch->app->title_image);
    if(launch->app->module != NULL) {
        lv_obj_add_flag(app_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(home_indicator, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_clear_flag(app_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(home_indicator, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(home_indicator, lv_color_hex(0x202124), LV_PART_MAIN);
    }
    start_app_transition(true);
    return true;
}

bool hachimi_desktop_launch(hachimi_app_id_t id)
{
    for(size_t i = 0; i < app_launch_count; i++) {
        if(app_launches[i].app->id == id) return launch_app(&app_launches[i]);
    }
    return false;
}

bool hachimi_desktop_home(void)
{
    if(hachimi_control_center_is_visible()) {
        hachimi_control_center_close();
        return true;
    }
    if(app_state != APP_STATE_OPEN) return false;
    close_app();
    return true;
}

static void open_app_event_cb(lv_event_t * event)
{
    (void)launch_app(lv_event_get_user_data(event));
}

static void app_gesture_event_cb(lv_event_t * event)
{
    if(app_state != APP_STATE_OPEN) return;

    const app_gesture_action_t * action = lv_event_get_user_data(event);
    lv_indev_t * indev = lv_indev_active();
    if(action == NULL || indev == NULL) return;

    lv_dir_t direction = lv_indev_get_gesture_dir(indev);
    if((*action == GESTURE_FROM_LEFT && direction == LV_DIR_RIGHT) ||
       (*action == GESTURE_FROM_RIGHT && direction == LV_DIR_LEFT) ||
       (*action == GESTURE_FROM_BOTTOM && direction == LV_DIR_TOP)) {
        close_app();
    }
}

static lv_obj_t * create_gesture_zone(lv_obj_t * parent, lv_coord_t x, lv_coord_t y,
                                      lv_coord_t width, lv_coord_t height,
                                      const app_gesture_action_t * action)
{
    lv_obj_t * zone = lv_obj_create(parent);
    lv_obj_remove_style_all(zone);
    lv_obj_set_pos(zone, x, y);
    lv_obj_set_size(zone, width, height);
    lv_obj_set_style_bg_opa(zone, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(zone, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(zone, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(zone, app_gesture_event_cb, LV_EVENT_GESTURE, (void *)action);
    return zone;
}

static void set_glass_panel(lv_obj_t * panel, lv_coord_t radius, lv_opa_t opacity)
{
    lv_obj_remove_style_all(panel);
    lv_obj_set_style_bg_color(panel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, opacity, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(panel, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, radius, LV_PART_MAIN);
}

static void create_app(lv_obj_t * parent, const hachimi_app_descriptor_t * app,
                       lv_coord_t center_x, lv_coord_t image_y)
{
    if(app_launch_count >= MAX_APP_LAUNCHES) return;
    app_launch_t * launch = &app_launches[app_launch_count++];
    launch->app = app;
    launch->x = center_x - ICON_SIZE / 2;
    launch->y = image_y;

    lv_obj_t * image = lv_image_create(parent);
    lv_image_set_src(image, app->image);
    lv_obj_set_pos(image, launch->x, image_y);
    lv_obj_add_flag(image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(image, open_app_event_cb, LV_EVENT_CLICKED, launch);

    if(app->label_image != NULL) {
        lv_obj_t * label = lv_image_create(parent);
        lv_image_set_src(label, app->label_image);
        lv_obj_align_to(label, image, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
        lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(label, open_app_event_cb, LV_EVENT_CLICKED, launch);
    }
}

static lv_color_t status_bar_foreground(lv_color_t background)
{
    lv_color32_t rgba = lv_color_to_32(background, LV_OPA_COVER);
    bool light_background = lv_color32_luminance(rgba) >= 128;
    return light_background ? lv_color_hex(0x202124) : lv_color_white();
}

static void create_status_bar(lv_obj_t * parent, lv_color_t background)
{
    lv_color_t foreground = status_bar_foreground(background);
    ios_status_bar_create(parent, foreground);
}

static void create_dock(lv_obj_t * parent)
{
    lv_obj_t * dock = lv_obj_create(parent);
    set_glass_panel(dock, 25, 95);
    lv_obj_set_style_shadow_color(dock, lv_color_hex(0x443c38), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(dock, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(dock, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_spread(dock, 0, LV_PART_MAIN);
    lv_obj_set_pos(dock, 197, 386);
    lv_obj_set_size(dock, 406, 86);
    lv_obj_clear_flag(dock, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    static const lv_coord_t centers[] = {245, 323, 400, 478, 556};
    size_t dock_count = 0;
    const hachimi_app_descriptor_t * dock_apps = hachimi_app_dock_list(&dock_count);
    for(size_t i = 0; i < dock_count && i < sizeof(centers) / sizeof(centers[0]); i++) {
        create_app(parent, &dock_apps[i], centers[i], 396);
    }
}

static void create_app_view(lv_obj_t * parent)
{
    transition_background = lv_image_create(parent);
    lv_obj_set_pos(transition_background, 0, 0);
    /* Let LVGL's cover search skip the original desktop underneath. */
    lv_obj_set_style_bg_opa(transition_background, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(transition_background, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(transition_background, LV_OBJ_FLAG_HIDDEN);
    app_view = lv_obj_create(parent);
    lv_obj_remove_style_all(app_view);
    lv_obj_set_size(app_view, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_pos(app_view, 0, 0);
    lv_obj_set_style_bg_color(app_view, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(app_view, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(app_view, 0, LV_PART_MAIN);
    lv_obj_clear_flag(app_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(app_view, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);

    app_title = lv_image_create(app_view);
    lv_obj_set_style_align(app_title, LV_ALIGN_TOP_LEFT, LV_PART_MAIN);
    lv_image_set_src(app_title, IOS_ASSET("labels/title_browser.png"));
    lv_obj_clear_flag(app_title, LV_OBJ_FLAG_CLICKABLE);

    hachimi_app_manager_init(&app_manager, app_view);

    home_indicator = lv_obj_create(app_view);
    lv_obj_remove_style_all(home_indicator);
    lv_obj_set_style_align(home_indicator, LV_ALIGN_TOP_LEFT, LV_PART_MAIN);
    lv_obj_set_size(home_indicator, 104, 5);
    lv_obj_set_style_bg_color(home_indicator, lv_color_hex(0x202124), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(home_indicator, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_radius(home_indicator, 3, LV_PART_MAIN);
    lv_obj_clear_flag(home_indicator, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    layout_app_content();

    app_mask = lv_obj_create(parent);
    lv_obj_remove_style_all(app_mask);
    lv_obj_set_size(app_mask, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_style_bg_color(app_mask, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(app_mask, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(app_mask, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(app_mask, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_clip_corner(app_mask, true, LV_PART_MAIN);
    transition_content = lv_image_create(app_mask);
    lv_obj_set_size(transition_content, lv_pct(100), lv_pct(100));
    lv_image_set_inner_align(transition_content, LV_IMAGE_ALIGN_STRETCH);
    lv_image_set_antialias(transition_content, false);
    lv_obj_clear_flag(transition_content, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(transition_content, LV_OBJ_FLAG_HIDDEN);

    gesture_zones[0] = create_gesture_zone(parent, 0, 60, EDGE_GESTURE_SIDE_SIZE,
                                          DISPLAY_HEIGHT - 60 - EDGE_GESTURE_BOTTOM_SIZE,
                                          &gesture_actions[0]);
    gesture_zones[1] = create_gesture_zone(parent, DISPLAY_WIDTH - EDGE_GESTURE_SIDE_SIZE, 60,
                                          EDGE_GESTURE_SIDE_SIZE,
                                          DISPLAY_HEIGHT - 60 - EDGE_GESTURE_BOTTOM_SIZE,
                                          &gesture_actions[1]);
    gesture_zones[2] = create_gesture_zone(parent, EDGE_GESTURE_SIDE_SIZE,
                                          DISPLAY_HEIGHT - EDGE_GESTURE_BOTTOM_SIZE,
                                          DISPLAY_WIDTH - EDGE_GESTURE_SIDE_SIZE * 2,
                                          EDGE_GESTURE_BOTTOM_SIZE, &gesture_actions[2]);
}

static bool control_center_can_open(void)
{
    return app_state == APP_STATE_IDLE || app_state == APP_STATE_OPEN;
}

bool hachimi_desktop_create(const hachimi_app_catalog_t * catalog)
{
    if(app_view != NULL || !hachimi_app_registry_configure(catalog)) return false;
    app_launch_count = 0;
    selected_launch = NULL;
    app_state = APP_STATE_IDLE;
    lv_obj_t * screen = lv_screen_active();
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen,lv_color_hex(0x202020),0);
    lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
    lv_obj_set_size(screen, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * wallpaper = lv_image_create(screen);
    lv_image_set_src(wallpaper, IOS_ASSET("wallpaper.png"));
    lv_obj_set_pos(wallpaper, 0, 0);

    /* The wallpaper's status-bar strip has a mean luminance of about 96/255. */
    create_status_bar(screen, lv_color_hex(0x606060));

    static const lv_coord_t centers[] = {87, 212, 337, 462, 587, 712};
    static const lv_coord_t rows[] = {59, 168, 276};
    size_t home_count = 0;
    const hachimi_app_descriptor_t * home_apps = hachimi_app_home_list(&home_count);
    for(size_t i = 0; i < home_count; i++) {
        size_t row = i / 6;
        size_t col = i % 6;
        if(row >= sizeof(rows) / sizeof(rows[0])) break;
        create_app(screen, &home_apps[i], centers[col], rows[row]);
    }

    create_dock(screen);
    create_app_view(screen);
    hachimi_app_system_start();
    hachimi_control_center_create(control_center_can_open);
    return true;
}

void hachimi_desktop_destroy(void)
{
    hachimi_control_center_destroy();
    finish_direct_transition();
    if(app_view == NULL) return;
    hachimi_app_system_stop();
    lv_anim_delete(&app_transition, app_transition_step);
    hachimi_app_manager_destroy(&app_manager);
    lv_obj_clean(lv_screen_active());
    if(transition_content_buffer != NULL) {
        lv_image_cache_drop(transition_content_buffer);
        lv_draw_buf_destroy(transition_content_buffer);
        transition_content_buffer = NULL;
    }
    if(transition_background_buffer != NULL) {
        lv_image_cache_drop(transition_background_buffer);
        lv_draw_buf_destroy(transition_background_buffer);
        transition_background_buffer = NULL;
    }
    transition_background = NULL;
    transition_content = NULL;
    app_view = app_mask = app_title = home_indicator = NULL;
    selected_launch = NULL;
    app_launch_count = 0;
    app_state = APP_STATE_IDLE;
}

void hachimi_desktop_set_transition_backend(const hachimi_desktop_transition_backend_t * backend)
{
    if(backend == NULL) memset(&transition_backend, 0, sizeof(transition_backend));
    else transition_backend = *backend;
}
