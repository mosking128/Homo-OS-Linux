#ifndef HACHIMI_IOS_CLOCK_H
#define HACHIMI_IOS_CLOCK_H

#include "framework/app.h"

extern const hachimi_app_module_t hachimi_clock_app;
/* Desktop lifetime: alarms must run before the Clock view is first opened. */
void hachimi_clock_runtime_start(void);
void hachimi_clock_runtime_stop(void);

#endif
