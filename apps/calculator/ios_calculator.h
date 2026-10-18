#ifndef HACHIMI_IOS_CALCULATOR_H
#define HACHIMI_IOS_CALCULATOR_H

#include "lvgl.h"
#include "framework/app.h"
extern const hachimi_app_module_t hachimi_calculator_app;

lv_obj_t * ios_calculator_create(lv_obj_t * parent, const lv_font_t * key_font);

#endif
