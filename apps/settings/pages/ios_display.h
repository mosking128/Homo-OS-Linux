#ifndef IOS_DISPLAY_H
#define IOS_DISPLAY_H

#include "lvgl.h"

void ios_display_create(lv_obj_t * parent, const lv_font_t * font,
                        const lv_font_t * small_font, const lv_font_t * heading_font,
                        const lv_font_t * title_font);
void ios_display_refresh(void);

#endif
