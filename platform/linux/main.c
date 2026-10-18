#define _DEFAULT_SOURCE

#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <unistd.h>
#include <time.h>

#include "lvgl.h"
#include "drivers/display/fb/lv_linux_fbdev.h"
#include "drivers/evdev/lv_evdev.h"
#include "framework/desktop.h"
#include "services/display.h"
#include "pxp/pxp_display.h"
#include "fb_transition.h"

static volatile sig_atomic_t shutdown_requested;

/* Optional board-side frame measurements; silent in normal operation. */
static double profile_clock(clockid_t clock)
{
    struct timespec value;
    clock_gettime(clock, &value);
    return value.tv_sec * 1000000.0 + value.tv_nsec / 1000.0;
}

static void profile_frame(lv_event_t * event)
{
    static double start, cpu, flush_start, flush_time;
    static unsigned pixels, rectangles;
    lv_event_code_t code = lv_event_get_code(event);
    if(code == LV_EVENT_RENDER_START) {
        start = profile_clock(CLOCK_MONOTONIC);
        cpu = profile_clock(CLOCK_PROCESS_CPUTIME_ID);
        flush_time = 0;
        pixels = rectangles = 0;
    }
    else if(code == LV_EVENT_FLUSH_START) {
        flush_start = profile_clock(CLOCK_MONOTONIC);
        const lv_area_t * area = lv_event_get_param(event);
        pixels += (unsigned)(area->x2-area->x1+1)*(area->y2-area->y1+1);
        rectangles++;
    }
    else if(code == LV_EVENT_FLUSH_FINISH) flush_time += profile_clock(CLOCK_MONOTONIC)-flush_start;
    else if(code == LV_EVENT_RENDER_READY) {
        double end = profile_clock(CLOCK_MONOTONIC);
        fprintf(stderr,"Hachimi-frame start_us=%.0f total_us=%.0f cpu_us=%.0f flush_us=%.0f pixels=%u rects=%u\n",
                start,end-start,profile_clock(CLOCK_PROCESS_CPUTIME_ID)-cpu,flush_time,pixels,rectangles);
    }
}

static void request_shutdown(int signal_number)
{
    (void)signal_number;
    shutdown_requested = 1;
}

typedef enum {
    TOUCH_RANGE_UNCHANGED,
    TOUCH_RANGE_FIXED,
    TOUCH_RANGE_LVGL_FALLBACK,
} touch_range_result_t;

static int set_abs_maximum(int fd, unsigned int axis, int maximum)
{
    struct input_absinfo info;
    if(ioctl(fd, EVIOCGABS(axis), &info) < 0) return -1;
    if(info.minimum == 0 && info.maximum == maximum) return 0;

    info.minimum = 0;
    info.maximum = maximum;
    if(ioctl(fd, EVIOCSABS(axis), &info) < 0) return -1;

    struct input_absinfo verified;
    if(ioctl(fd, EVIOCGABS(axis), &verified) < 0) return -1;
    if(verified.minimum == 0 && verified.maximum == maximum) return 0;
    errno = ERANGE;
    return -1;
}

static int set_goodix_abs_ranges(int fd, int width, int height)
{
    const unsigned int axes[] = {
        ABS_MT_POSITION_X, ABS_MT_POSITION_Y, ABS_X, ABS_Y,
    };
    const int maxima[] = {width - 1, height - 1, width - 1, height - 1};
    for(size_t i = 0; i < sizeof(axes) / sizeof(axes[0]); i++) {
        if(set_abs_maximum(fd, axes[i], maxima[i]) < 0) return -1;
    }
    return 0;
}

static touch_range_result_t prepare_touch_range(const char *path, int width, int height)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if(fd < 0) {
        fprintf(stderr, "Hachimi: cannot inspect touch device %s: %s\n", path, strerror(errno));
        return TOUCH_RANGE_UNCHANGED;
    }

    char name[128] = {0};
    if(ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0 || strstr(name, "Goodix") == NULL) {
        close(fd);
        return TOUCH_RANGE_UNCHANGED;
    }

    /*
     * This board's Goodix config blob is missing. Its events already use the
     * framebuffer coordinate space, while the kernel advertises a mismatched
     * ABS range. Correct it before LVGL reads EVIOCGABS.
     */
    if(set_goodix_abs_ranges(fd, width, height) == 0) {
        close(fd);
        return TOUCH_RANGE_FIXED;
    }

    int read_only_error = errno;
    close(fd);

    /* The board's proven touchfix first tries O_RDONLY, then O_RDWR. */
    fd = open(path, O_RDWR | O_CLOEXEC);
    if(fd >= 0 && ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0 &&
       strstr(name, "Goodix") != NULL && set_goodix_abs_ranges(fd, width, height) == 0) {
        close(fd);
        return TOUCH_RANGE_FIXED;
    }

    int write_error = errno;
    if(fd >= 0) close(fd);
    fprintf(stderr, "Hachimi: could not update Goodix ABS range (read-only: %s; read-write: %s)\n",
            strerror(read_only_error), strerror(write_error));
    return TOUCH_RANGE_LVGL_FALLBACK;
}

int main(void)
{
    struct sigaction action = {0};
    action.sa_handler = request_shutdown;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    if(hachimi_display_restore_brightness())
        fprintf(stderr, "Hachimi: restored saved display brightness\n");
    lv_init();

    lv_display_t * display = lv_linux_fbdev_create();
    if(display == NULL) {
        fprintf(stderr, "Hachimi: failed to create the framebuffer display\n");
        return EXIT_FAILURE;
    }

    const char * fb_path = getenv("HACHIMI_FBDEV");
    lv_linux_fbdev_set_file(display, fb_path != NULL ? fb_path : "/dev/fb0");
    lv_display_set_default(display);
#if HACHIMI_USE_PXP
    hachimi_pxp_display_attach(display, fb_path != NULL ? fb_path : "/dev/fb0");
#endif
    hachimi_fb_transition_attach(display, fb_path != NULL ? fb_path : "/dev/fb0");
    const char * profile = getenv("HACHIMI_RENDER_PROFILE");
    if(profile && strcmp(profile,"1") == 0) lv_display_add_event_cb(display,profile_frame,LV_EVENT_ALL,NULL);

    const char * touch_path = getenv("HACHIMI_EVDEV");
    if(touch_path == NULL) touch_path = "/dev/input/event2";
    int touch_width = lv_display_get_horizontal_resolution(display);
    int touch_height = lv_display_get_vertical_resolution(display);
    touch_range_result_t range_result = prepare_touch_range(touch_path, touch_width, touch_height);

    lv_indev_t * touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, touch_path);
    if(touch != NULL) {
        if(range_result == TOUCH_RANGE_FIXED) {
            fprintf(stderr, "Hachimi: corrected Goodix touch range to %dx%d\n",
                    touch_width, touch_height);
        }
        else if(range_result == TOUCH_RANGE_LVGL_FALLBACK) {
            /* The four-corner board calibration shows native 800x480 coords. */
            lv_evdev_set_calibration(touch, 0, 0, touch_width - 1, touch_height - 1);
        }
        lv_indev_set_display(touch, display);
    }
    else {
        fprintf(stderr, "Hachimi: touch input unavailable; desktop remains display-only\n");
    }

    ios_desktop_create();

    while(!shutdown_requested) {
        uint32_t sleep_ms = lv_timer_handler();
        if(sleep_ms == 0) sleep_ms = 1;
        if(sleep_ms > 16) sleep_ms = 16;
        usleep(sleep_ms * 1000U);
    }

    /* In particular, silence the GPIO buzzer when systemd stops/restarts us. */
    hachimi_desktop_destroy();
    hachimi_fb_transition_detach();
#if HACHIMI_USE_PXP
    hachimi_pxp_display_detach();
#endif
    return EXIT_SUCCESS;
}
