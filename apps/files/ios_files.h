#ifndef IOS_FILES_H
#define IOS_FILES_H

#include "lvgl.h"
#include "framework/app.h"
extern const hachimi_app_module_t hachimi_files_app;

lv_obj_t * ios_files_create(lv_obj_t * parent, const lv_font_t * font,
                            const lv_font_t * small_font, const lv_font_t * heading_font);
void ios_files_refresh(void);

#endif
