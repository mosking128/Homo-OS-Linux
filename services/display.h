#ifndef HACHIMI_DISPLAY_SERVICE_H
#define HACHIMI_DISPLAY_SERVICE_H
#include <stdbool.h>
#define HACHIMI_DISPLAY_MIN_PERCENT 20
bool hachimi_display_get_brightness(int * percent);
bool hachimi_display_set_brightness(int percent);
bool hachimi_display_restore_brightness(void);
#endif
