#ifndef HACHIMI_CONTROL_CENTER_H
#define HACHIMI_CONTROL_CENTER_H

#include <stdbool.h>

void hachimi_control_center_create(bool (*can_open)(void));
void hachimi_control_center_destroy(void);
bool hachimi_control_center_is_visible(void);
void hachimi_control_center_close(void);

#endif
