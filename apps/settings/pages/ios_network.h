#ifndef IOS_NETWORK_H
#define IOS_NETWORK_H

#include "lvgl.h"

void ios_network_create(lv_obj_t * parent, const lv_font_t * font,
                        const lv_font_t * small_font, const lv_font_t * heading_font);
void ios_network_refresh(void);

#endif
