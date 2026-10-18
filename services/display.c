#include "display.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define BACKLIGHT_PATH "/sys/class/backlight/backlight/brightness"
#define BACKLIGHT_MAX_PATH "/sys/class/backlight/backlight/max_brightness"
#define BRIGHTNESS_STATE_DIR "/var/lib/hachimi"
#define BRIGHTNESS_STATE_PATH BRIGHTNESS_STATE_DIR "/display-brightness"
#define MIN_BRIGHTNESS_PERCENT 20
static bool read_brightness_file(const char * path, int * value)
{
    FILE * file = fopen(path, "r");
    if(file == NULL) return false;

    int result = fscanf(file, "%d", value);
    fclose(file);
    return result == 1;
}

static bool save_brightness(int percent)
{
#ifdef _WIN32
    (void)percent;
    return false; /* The simulator has no Linux backlight state directory. */
#else
    if(mkdir(BRIGHTNESS_STATE_DIR, 0755) < 0 && errno != EEXIST) return false;
    char temporary[sizeof(BRIGHTNESS_STATE_PATH) + 8];
    if(snprintf(temporary, sizeof(temporary), "%s.tmp", BRIGHTNESS_STATE_PATH) >=
       (int)sizeof(temporary)) return false;

    int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if(fd < 0) return false;
    char value[8];
    int length = snprintf(value, sizeof(value), "%d\n", percent);
    bool success = length > 0 && (size_t)length < sizeof(value) &&
                   write(fd, value, (size_t)length) == (ssize_t)length && fsync(fd) == 0;
    if(close(fd) < 0) success = false;
    if(success && rename(temporary, BRIGHTNESS_STATE_PATH) < 0) success = false;
    if(!success) unlink(temporary);
    else {
        int dir_fd = open(BRIGHTNESS_STATE_DIR, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if(dir_fd >= 0) { (void)fsync(dir_fd); close(dir_fd); }
    }
    return success;
#endif
}

static bool load_brightness(int * percent)
{
    int saved = 0;
    if(!read_brightness_file(BRIGHTNESS_STATE_PATH, &saved) ||
       saved < MIN_BRIGHTNESS_PERCENT || saved > 100) return false;
    *percent = saved;
    return true;
}

bool hachimi_display_set_brightness(int percent)
{
    int max_brightness = 0;
    if(!read_brightness_file(BACKLIGHT_MAX_PATH, &max_brightness) || max_brightness <= 0) return false;

    if(percent < MIN_BRIGHTNESS_PERCENT) percent = MIN_BRIGHTNESS_PERCENT;
    if(percent > 100) percent = 100;
    int value = (max_brightness * percent + 50) / 100;
    int minimum = (max_brightness * MIN_BRIGHTNESS_PERCENT + 99) / 100;
    if(minimum < 1) minimum = 1;
    if(value < minimum) value = minimum;
    if(value > max_brightness) value = max_brightness;

    FILE * file = fopen(BACKLIGHT_PATH, "w");
    if(file == NULL) return false;
    bool success = fprintf(file, "%d\n", value) > 0;
    if(fclose(file) != 0) success = false;
    if(success) success = save_brightness(percent);
    return success;
}


bool hachimi_display_get_brightness(int * percent)
{
    int maximum = 0, value = 0;
    if(!read_brightness_file(BACKLIGHT_MAX_PATH, &maximum) ||
       !read_brightness_file(BACKLIGHT_PATH, &value) || maximum <= 0) return false;
    *percent = (value * 100 + maximum / 2) / maximum;
    if(*percent < MIN_BRIGHTNESS_PERCENT) {
        *percent = MIN_BRIGHTNESS_PERCENT;
        if(!hachimi_display_set_brightness(*percent)) return false;
    }
    if(*percent > 100) *percent = 100;
    return true;
}

bool hachimi_display_restore_brightness(void)
{
    int percent;
    return load_brightness(&percent) && hachimi_display_set_brightness(percent);
}
