#ifndef HACHIMI_PXP_DISPLAY_H
#define HACHIMI_PXP_DISPLAY_H
#include "lvgl.h"
int hachimi_pxp_display_attach(lv_display_t * display, const char * framebuffer);
void hachimi_pxp_display_detach(void);
#endif
