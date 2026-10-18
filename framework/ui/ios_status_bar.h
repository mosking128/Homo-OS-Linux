#ifndef HACHIMI_IOS_STATUS_BAR_H
#define HACHIMI_IOS_STATUS_BAR_H

#include "lvgl.h"

void ios_status_bar_create_time_label(lv_obj_t * parent, lv_color_t foreground);
void ios_status_bar_create(lv_obj_t * parent, lv_color_t foreground);

#endif
