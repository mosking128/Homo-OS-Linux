#ifndef IOS_TIME_H
#define IOS_TIME_H

#include "lvgl.h"

void ios_time_create(lv_obj_t * parent, const lv_font_t * font,
                     const lv_font_t * small_font, const lv_font_t * heading_font,
                     const lv_font_t * title_font);
void ios_time_refresh(void);

#endif
