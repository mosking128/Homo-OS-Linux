#ifndef HACHIMI_FB_TRANSITION_H
#define HACHIMI_FB_TRANSITION_H

#include "lvgl.h"

void hachimi_fb_transition_attach(lv_display_t * display, const char * framebuffer);
void hachimi_fb_transition_detach(void);

#endif
