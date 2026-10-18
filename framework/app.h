#ifndef HACHIMI_APP_H
#define HACHIMI_APP_H
#include "lvgl.h"

/* Apps own their view and state. The framework owns visibility and lifetime.
 * create is called once on first launch; views are retained until destroy.
 * Release view timers from the root view's LV_EVENT_DELETE callback.
 * Optional system hooks own background work for the desktop's full lifetime. */
typedef struct {
    const char * id;
    lv_obj_t * (*create)(lv_obj_t * parent);
    void (*on_open)(lv_obj_t * view);
    void (*on_close)(lv_obj_t * view);
    void (*on_system_start)(void);
    void (*on_system_stop)(void);
} hachimi_app_module_t;
#endif
