#define _DEFAULT_SOURCE
#include "fb_transition.h"

#include "framework/desktop.h"

#include <errno.h>
#include <float.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define FB_TRANSITION_WIDTH 800
#define FB_TRANSITION_HEIGHT 480
#define FB_TRANSITION_ANIM_PERIOD 8
#define FB_TRANSITION_MIN_FRAME_US 16000.0

typedef struct {
    int x, y, width, height;
} fb_transition_rect_t;

typedef struct {
    lv_display_t * display;
    int fd;
    uint8_t * pixels;
    uint8_t * background;
    const lv_draw_buf_t * content;
    unsigned source_x[FB_TRANSITION_WIDTH];
    size_t map_size;
    size_t page_size;
    uint32_t stride;
    int width, height;
    struct fb_var_screeninfo original_variable;
    struct fb_var_screeninfo page_variable;
    fb_transition_rect_t page_rect[2];
    unsigned front_page, back_page;
    uint32_t animation_period;
    unsigned long frames;
    double start_wall_us, start_cpu_us, max_frame_us;
    double last_flip_us, interval_sum_us, min_interval_us, max_interval_us;
    double last_vsync_us;
    int active, vsync_available, double_buffered, page_flip_pending, pending_is_animation;
} fb_transition_state_t;

static fb_transition_state_t state = {.fd = -1};

static double clock_us(clockid_t id)
{
    struct timespec now;
    clock_gettime(id, &now);
    return now.tv_sec * 1000000.0 + now.tv_nsec / 1000.0;
}

static int env_is_disabled(void)
{
    const char * value = getenv("HACHIMI_DIRECT_TRANSITION");
    return value != NULL && strcmp(value, "0") == 0;
}

static int wait_for_vsync(void)
{
    uint32_t argument = 0;
    return ioctl(state.fd, FBIO_WAITFORVSYNC, &argument) == 0;
}

static int wait_for_frame_vsync(void)
{
    for(unsigned attempt = 0; attempt < 3; attempt++) {
        if(!wait_for_vsync()) return 0;
        double now = clock_us(CLOCK_MONOTONIC);
        if(state.last_vsync_us == 0.0 || now - state.last_vsync_us >= FB_TRANSITION_MIN_FRAME_US) {
            state.last_vsync_us = now;
            return 1;
        }
    }
    errno = EAGAIN;
    return 0;
}

static int pan_to_page(unsigned page, uint32_t activate)
{
    struct fb_var_screeninfo variable = state.page_variable;
    variable.yoffset = page * (unsigned)state.height;
    variable.activate = activate;
    return ioctl(state.fd, FBIOPAN_DISPLAY, &variable);
}

/* A handoff must retire the old scanout page before LVGL or background
 * restoration can write to it. An already-pending VSYNC IRQ can make the
 * first wait return immediately; require a full interval from submission.
 * This extra fence is used only at ownership changes, not on animation frames. */
static int present_handoff_page(unsigned page)
{
    double submitted = clock_us(CLOCK_MONOTONIC);
    if(pan_to_page(page, FB_ACTIVATE_NOW) < 0) return 0;
    for(unsigned attempt = 0; attempt < 3; attempt++) {
        if(!wait_for_vsync()) return 0;
        double now = clock_us(CLOCK_MONOTONIC);
        if(now - submitted >= FB_TRANSITION_MIN_FRAME_US) {
            state.last_vsync_us = now;
            return 1;
        }
    }
    errno = EAGAIN;
    return 0;
}

static void restore_framebuffer_mode(int fd, const struct fb_var_screeninfo * original)
{
    struct fb_var_screeninfo variable = *original;
    variable.yoffset = 0;
    variable.activate = FB_ACTIVATE_NOW;
    ioctl(fd, FBIOPUT_VSCREENINFO, &variable);
}

static void restore_page(unsigned page)
{
    fb_transition_rect_t * rect = &state.page_rect[page];
    if(rect->width <= 0 || rect->height <= 0) return;
    uint8_t * page_pixels = state.pixels + (size_t)page * state.page_size;
    size_t bytes = (size_t)rect->width * 2;
    for(int row = 0; row < rect->height; row++) {
        size_t offset = (size_t)(rect->y + row) * state.stride + (size_t)rect->x * 2;
        memcpy(page_pixels + offset, state.background + offset, bytes);
    }
}

static void record_visible_frame(void)
{
    double flip_us = clock_us(CLOCK_MONOTONIC);
    if(state.last_flip_us > 0.0) {
        double interval = flip_us - state.last_flip_us;
        state.interval_sum_us += interval;
        if(interval < state.min_interval_us) state.min_interval_us = interval;
        if(interval > state.max_interval_us) state.max_interval_us = interval;
    }
    state.last_flip_us = flip_us;
    state.frames++;
}

static uint16_t blend_color(uint16_t source, uint16_t target, unsigned alpha)
{
    unsigned red = (source >> 11) & 31U;
    unsigned green = (source >> 5) & 63U;
    unsigned blue = source & 31U;
    red = (red * (255U - alpha) + ((target >> 11) & 31U) * alpha + 127U) / 255U;
    green = (green * (255U - alpha) + ((target >> 5) & 63U) * alpha + 127U) / 255U;
    blue = (blue * (255U - alpha) + (target & 31U) * alpha + 127U) / 255U;
    return (uint16_t)((red << 11) | (green << 5) | blue);
}

static unsigned horizontal_coverage(double left, double right, int pixel)
{
    double covered = fmin(right, pixel + 1.0) - fmax(left, (double)pixel);
    if(covered <= 0.0) return 0;
    if(covered >= 1.0) return 255;
    return (unsigned)(covered * 255.0 + 0.5);
}

static void fill_card_row(uint8_t * page_pixels, int screen_y, int screen_x, int width,
                           int rect_x, int rect_y,
                           int rect_width, int rect_height, int radius)
{
    double edge = 0.0;
    int row = screen_y - rect_y;
    int corner_y = row;
    if(row < radius) corner_y = row;
    else if(row >= rect_height - radius) corner_y = rect_height - row - 1;
    else corner_y = -1;
    if(corner_y >= 0 && radius > 0) {
        double dy = radius - (corner_y + 0.5);
        double inside = (double)radius * radius - dy * dy;
        if(inside < 0.0) inside = 0.0;
        edge = radius - sqrt(inside);
    }

    double left = edge;
    double right = rect_width - edge;
    int first = screen_x - rect_x;
    int last = first + width;
    if(first < 0) first = 0;
    if(last > rect_width) last = rect_width;
    if(last <= first) return;

    uint16_t * row_pixels = (uint16_t *)(page_pixels + (size_t)screen_y * state.stride + (size_t)screen_x * 2);
    const uint16_t * source = NULL;
    if(state.content != NULL) {
        unsigned source_y = (unsigned)row * state.content->header.h / (unsigned)rect_height;
        source = (const uint16_t *)(state.content->data + (size_t)source_y * state.content->header.stride);
    }
    int full_start = (int)ceil(left);
    int full_end = (int)floor(right);
    if(full_start < first) full_start = first;
    if(full_end > last) full_end = last;

    int left_pixel = (int)floor(left);
    if(left_pixel >= first && left_pixel < last) {
        unsigned alpha = horizontal_coverage(left, right, left_pixel);
        uint16_t color = source ? source[state.source_x[left_pixel - first]] : 0xffff;
        row_pixels[left_pixel - first] = blend_color(row_pixels[left_pixel - first], color, alpha);
    }
    if(full_end > full_start) {
        int start = full_start;
        int end = full_end;
        if(start == left_pixel) start++;
        int right_pixel = (int)floor(right);
        if(end > right_pixel) end = right_pixel;
        if(end > start) {
            if(source == NULL) memset(row_pixels + (start - first), 0xff, (size_t)(end - start) * 2);
            else if(rect_width == state.width)
                memcpy(row_pixels + start - first, source + start, (size_t)(end - start) * 2);
            else for(int column = start; column < end; column++)
                row_pixels[column - first] = source[state.source_x[column - first]];
        }
    }
    int right_pixel = (int)floor(right);
    if(right_pixel >= first && right_pixel < last && right_pixel != left_pixel) {
        unsigned alpha = horizontal_coverage(left, right, right_pixel);
        uint16_t color = source ? source[state.source_x[right_pixel - first]] : 0xffff;
        row_pixels[right_pixel - first] = blend_color(row_pixels[right_pixel - first], color, alpha);
    }
}

static void draw_card(unsigned page, int32_t x, int32_t y, int32_t width, int32_t height,
                            int32_t radius, fb_transition_rect_t * page_rect)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width > state.width ? state.width : x + width;
    int y1 = y + height > state.height ? state.height : y + height;
    if(x1 <= x0 || y1 <= y0) {
        page_rect->width = page_rect->height = 0;
        return;
    }

    int clamped_radius = radius;
    if(clamped_radius > width / 2) clamped_radius = width / 2;
    if(clamped_radius > height / 2) clamped_radius = height / 2;
    if(clamped_radius < 0) clamped_radius = 0;

    uint8_t * page_pixels = state.pixels + (size_t)page * state.page_size;
    if(state.content != NULL) {
        for(int column = x0; column < x1; column++)
            state.source_x[column - x0] = (unsigned)(column - x) * state.content->header.w / (unsigned)width;
    }
    for(int row = y0; row < y1; row++) {
        fill_card_row(page_pixels, row, x0, x1 - x0, x, y, width, height, clamped_radius);
    }
    page_rect->x = x0;
    page_rect->y = y0;
    page_rect->width = x1 - x0;
    page_rect->height = y1 - y0;
}

static void direct_draw(void * user_data, int32_t x, int32_t y, int32_t width, int32_t height,
                        int32_t radius, bool reveal_target)
{
    (void)user_data;
    if(!state.active || width <= 0 || height <= 0) return;
    /* Complete the prior flip before writing to its old front page. */
    if(!wait_for_frame_vsync()) {
        fprintf(stderr, "Hachimi: waiting for framebuffer VSYNC failed (%s); skipping frame\n", strerror(errno));
        return;
    }
    if(state.page_flip_pending) {
        if(state.pending_is_animation) record_visible_frame();
        state.page_flip_pending = 0;
        state.pending_is_animation = 0;
    }
    double frame_start = clock_us(CLOCK_MONOTONIC);
    unsigned page = state.back_page;
    fb_transition_rect_t * page_rect = &state.page_rect[page];
    restore_page(page);

    if(!reveal_target) {
        draw_card(page, x, y, width, height, radius, page_rect);
    }
    else {
        /* The final close frame reveals the source icon directly instead of
         * presenting a white icon for one more refresh interval. */
        page_rect->width = 0;
        page_rect->height = 0;
    }

    /* MXS LCDIF queues the buffer base for the next VSYNC. Keep this ioctl
     * non-blocking, then wait once below before touching the old front page. */
    if(pan_to_page(page, FB_ACTIVATE_NOW) < 0) {
        fprintf(stderr, "Hachimi: framebuffer page flip failed (%s); skipping frame\n", strerror(errno));
        return;
    }
    state.front_page = page;
    state.back_page = page ^ 1U;
    state.page_flip_pending = 1;
    state.pending_is_animation = 1;

    double duration = clock_us(CLOCK_MONOTONIC) - frame_start;
    if(duration > state.max_frame_us) state.max_frame_us = duration;
}

static bool direct_begin(void * user_data, const lv_draw_buf_t * background,
                         const lv_draw_buf_t * content,
                         int32_t x, int32_t y, int32_t width, int32_t height, int32_t radius)
{
    (void)user_data;
    if(state.active || env_is_disabled() || background == NULL || background->data == NULL ||
       background->header.cf != LV_COLOR_FORMAT_RGB565 ||
       background->header.w != (uint32_t)state.width ||
       background->header.h != (uint32_t)state.height || background->header.stride < (uint32_t)state.width * 2) return false;
    state.content = NULL;
    if(content != NULL && content->data != NULL && content->header.cf == LV_COLOR_FORMAT_RGB565 &&
       content->header.w == (uint32_t)state.width && content->header.h == (uint32_t)state.height &&
       content->header.stride >= (uint32_t)state.width * 2) state.content = content;

    for(int row = 0; row < state.height; row++) {
        memcpy(state.background + (size_t)row * state.stride,
               state.pixels + (size_t)row * state.stride, state.stride);
        memcpy(state.background + (size_t)row * state.stride,
               background->data + (size_t)row * background->header.stride,
               (size_t)state.width * 2);
    }
    memset(state.page_rect, 0, sizeof(state.page_rect));
    struct fb_var_screeninfo current_variable;
    if(ioctl(state.fd, FBIOGET_VSCREENINFO, &current_variable) < 0 ||
       current_variable.yoffset % (unsigned)state.height != 0 ||
       current_variable.yoffset / (unsigned)state.height > 1U) return false;
    state.front_page = current_variable.yoffset / (unsigned)state.height;
    state.back_page = state.front_page ^ 1U;
    unsigned initial_page = state.back_page;
    uint8_t * initial_pixels = state.pixels + (size_t)initial_page * state.page_size;
    memcpy(initial_pixels, state.background, state.page_size);
    draw_card(initial_page, x, y, width, height, radius, &state.page_rect[initial_page]);
    state.page_flip_pending = 0;
    state.pending_is_animation = 0;
    state.frames = 0;
    state.max_frame_us = 0;
    state.last_flip_us = 0.0;
    state.interval_sum_us = 0.0;
    state.min_interval_us = DBL_MAX;
    state.max_interval_us = 0.0;
    state.last_vsync_us = 0.0;

    lv_timer_t * refresh_timer = lv_display_get_refr_timer(state.display);
    if(refresh_timer != NULL) lv_timer_pause(refresh_timer);

    /* Build and present the initial card off-screen. In particular, on close
     * this replaces the app directly with its full-screen snapshot instead
     * of briefly exposing the cached desktop while the first animation tick
     * waits for VSYNC. */
    if(!present_handoff_page(initial_page)) {
        fprintf(stderr, "Hachimi: framebuffer page flip unsupported (%s); using LVGL rendering\n", strerror(errno));
        if(refresh_timer != NULL) lv_timer_resume(refresh_timer);
        return false;
    }
    state.front_page = initial_page;
    state.back_page = initial_page ^ 1U;
    state.last_vsync_us = clock_us(CLOCK_MONOTONIC);
    /* The handoff fence has retired the old front page. Clear it only now,
     * before starting the animation clock, so the first frame does not pay
     * for a full-screen restore as well as resampling the application. */
    memcpy(state.pixels + (size_t)state.back_page * state.page_size, state.background, state.page_size);
    state.page_rect[state.back_page].width = 0;
    state.page_rect[state.back_page].height = 0;

    state.start_wall_us = clock_us(CLOCK_MONOTONIC);
    state.start_cpu_us = clock_us(CLOCK_PROCESS_CPUTIME_ID);
    lv_timer_t * anim_timer = lv_anim_get_timer();
    if(anim_timer != NULL) {
        state.animation_period = LV_DEF_REFR_PERIOD;
        lv_timer_set_period(anim_timer, FB_TRANSITION_ANIM_PERIOD);
    }
    state.active = 1;
    return true;
}

static void direct_end(void * user_data)
{
    (void)user_data;
    if(!state.active) return;
    if(state.page_flip_pending) {
        if(wait_for_frame_vsync()) {
            if(state.pending_is_animation) record_visible_frame();
            state.page_flip_pending = 0;
            state.pending_is_animation = 0;
        }
        else {
            fprintf(stderr, "Hachimi: waiting for the final animation page flip failed (%s)\n", strerror(errno));
        }
    }

    /* Leave the final animation frame visible on page 1. LVGL's framebuffer
     * driver always renders to page 0, so desktop/app rendering can complete
     * off-screen before present_target() flips to it at VSYNC. */
    if(state.front_page != 1U) {
        memcpy(state.pixels + state.page_size, state.pixels + (size_t)state.front_page * state.page_size,
               state.page_size);
        if(present_handoff_page(1)) {
            state.front_page = 1;
            state.back_page = 0;
            state.page_flip_pending = 0;
            state.pending_is_animation = 0;
        }
        else {
            fprintf(stderr, "Hachimi: could not hold the final animation frame (%s)\n", strerror(errno));
        }
    }
    state.active = 0;
    lv_timer_t * anim_timer = lv_anim_get_timer();
    if(anim_timer != NULL) lv_timer_set_period(anim_timer, state.animation_period);
    lv_timer_t * refresh_timer = lv_display_get_refr_timer(state.display);
    if(refresh_timer != NULL) lv_timer_resume(refresh_timer);
    double elapsed = state.last_flip_us > state.start_wall_us ? state.last_flip_us - state.start_wall_us : 0.0;
    double cpu = clock_us(CLOCK_PROCESS_CPUTIME_ID) - state.start_cpu_us;
    fprintf(stderr, "Hachimi-direct-transition frames=%lu wall_ms=%.1f fps=%.1f cpu_ms=%.1f max_frame_ms=%.3f cadence_ms=%.2f/%.2f/%.2f\n",
            state.frames, elapsed / 1000.0, elapsed > 0 ? state.frames * 1000000.0 / elapsed : 0.0,
            cpu / 1000.0, state.max_frame_us / 1000.0,
            state.frames > 1 ? state.min_interval_us / 1000.0 : 0.0,
            state.frames > 1 ? state.interval_sum_us / (state.frames - 1) / 1000.0 : 0.0,
            state.max_interval_us / 1000.0);
}

static void direct_present(void * user_data)
{
    (void)user_data;
    if(!state.double_buffered) return;
    state.page_flip_pending = 1;
    state.pending_is_animation = 0;
    if(!present_handoff_page(0)) {
        fprintf(stderr, "Hachimi: waiting for target page flip failed (%s)\n", strerror(errno));
        return;
    }
    state.front_page = 0;
    state.back_page = 1;
    state.page_flip_pending = 0;
    /* Page 1 is scratch for the next transition. Do not overwrite it here:
     * it was the visible page immediately before this ownership change. */
}

void hachimi_fb_transition_attach(lv_display_t * display, const char * framebuffer)
{
    if(display == NULL || framebuffer == NULL || state.fd >= 0 || env_is_disabled()) return;
    int fd = open(framebuffer, O_RDWR | O_CLOEXEC);
    if(fd < 0) return;
    struct fb_fix_screeninfo fixed;
    struct fb_var_screeninfo variable;
    int valid = ioctl(fd, FBIOGET_FSCREENINFO, &fixed) == 0 && ioctl(fd, FBIOGET_VSCREENINFO, &variable) == 0;
    if(!valid || variable.bits_per_pixel != 16 || variable.xoffset || variable.yoffset ||
       variable.red.offset != 11 || variable.red.length != 5 || variable.green.offset != 5 ||
       variable.green.length != 6 || variable.blue.offset != 0 || variable.blue.length != 5 ||
       variable.xres != FB_TRANSITION_WIDTH || variable.yres != FB_TRANSITION_HEIGHT ||
       fixed.line_length < FB_TRANSITION_WIDTH * 2 || fixed.smem_len < fixed.line_length * FB_TRANSITION_HEIGHT) {
        close(fd);
        return;
    }

    struct fb_var_screeninfo original_variable = variable;
    if(variable.yres_virtual < variable.yres * 2) {
        variable.yres_virtual = variable.yres * 2;
        variable.yoffset = 0;
        variable.activate = FB_ACTIVATE_NOW;
        if(ioctl(fd, FBIOPUT_VSCREENINFO, &variable) < 0 ||
           ioctl(fd, FBIOGET_VSCREENINFO, &variable) < 0) {
            restore_framebuffer_mode(fd, &original_variable);
            close(fd);
            return;
        }
    }
    if(variable.yres_virtual < variable.yres * 2 || variable.yoffset != 0 ||
       ioctl(fd, FBIOGET_FSCREENINFO, &fixed) < 0 || fixed.ypanstep == 0 ||
       fixed.smem_len < fixed.line_length * variable.yres * 2) {
        restore_framebuffer_mode(fd, &original_variable);
        close(fd);
        return;
    }

    void * mapping = mmap(NULL, fixed.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(mapping == MAP_FAILED) {
        restore_framebuffer_mode(fd, &original_variable);
        close(fd);
        return;
    }
    size_t page_size = (size_t)fixed.line_length * FB_TRANSITION_HEIGHT;
    uint8_t * background = malloc(page_size);
    if(background == NULL) {
        munmap(mapping, fixed.smem_len);
        restore_framebuffer_mode(fd, &original_variable);
        close(fd);
        return;
    }

    state.display = display;
    state.fd = fd;
    state.pixels = mapping;
    state.background = background;
    state.map_size = fixed.smem_len;
    state.page_size = page_size;
    state.stride = fixed.line_length;
    state.width = variable.xres;
    state.height = variable.yres;
    state.original_variable = original_variable;
    state.page_variable = variable;
    state.double_buffered = 1;
    uint32_t vsync = 0;
    state.vsync_available = ioctl(fd, FBIO_WAITFORVSYNC, &vsync) == 0;
    if(!state.vsync_available) {
        fprintf(stderr, "Hachimi: framebuffer vsync unavailable; direct transition disabled\n");
        munmap(state.pixels, state.map_size);
        free(state.background);
        restore_framebuffer_mode(fd, &original_variable);
        close(state.fd);
        memset(&state, 0, sizeof(state));
        state.fd = -1;
        return;
    }
    hachimi_desktop_transition_backend_t backend = {
        .user_data = &state,
        .begin = direct_begin,
        .draw = direct_draw,
        .end = direct_end,
        .present = direct_present,
    };
    hachimi_desktop_set_transition_backend(&backend);
    fprintf(stderr, "Hachimi: tear-free framebuffer transition available on %s (%dx%d, stride %u, two pages + vsync)\n",
            framebuffer, state.width, state.height, state.stride);
}

void hachimi_fb_transition_detach(void)
{
    if(state.active) direct_end(&state);
    hachimi_desktop_set_transition_backend(NULL);
    if(state.fd >= 0 && state.double_buffered) {
        pan_to_page(0, FB_ACTIVATE_NOW);
        restore_framebuffer_mode(state.fd, &state.original_variable);
    }
    if(state.pixels != NULL) munmap(state.pixels, state.map_size);
    free(state.background);
    if(state.fd >= 0) close(state.fd);
    memset(&state, 0, sizeof(state));
    state.fd = -1;
}
