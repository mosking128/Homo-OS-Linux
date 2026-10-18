#ifndef HACHIMI_APP_REGISTRY_H
#define HACHIMI_APP_REGISTRY_H

#include <stddef.h>
#include <stdbool.h>
#include "app.h"

typedef enum {
    HACHIMI_APP_BROWSER,
    HACHIMI_APP_BOOKS,
    HACHIMI_APP_CALCULATOR,
    HACHIMI_APP_CALENDAR,
    HACHIMI_APP_CAMERA,
    HACHIMI_APP_CLOCK,
    HACHIMI_APP_FILES,
    HACHIMI_APP_HEALTH,
    HACHIMI_APP_LED_CONTROL,
    HACHIMI_APP_MAPS,
    HACHIMI_APP_MESSAGES,
    HACHIMI_APP_MUSIC,
    HACHIMI_APP_SETTINGS,
    HACHIMI_APP_WEATHER,
    HACHIMI_APP_MAIL,
} hachimi_app_id_t;

typedef struct {
    hachimi_app_id_t id;
    const char * image;
    const char * label_image;
    const char * title_image;
    const hachimi_app_module_t * module;
    bool reserve_left_edge; /* App navigation owns the left edge. */
} hachimi_app_descriptor_t;

/* Descriptor arrays and module pointers are borrowed for the desktop lifetime.
 * Background modules are explicit and unique, independent of launcher visibility. */
typedef struct {
    const hachimi_app_descriptor_t * home;
    size_t home_count;
    const hachimi_app_descriptor_t * dock;
    size_t dock_count;
    const hachimi_app_module_t * const * background;
    size_t background_count;
} hachimi_app_catalog_t;

bool hachimi_app_registry_configure(const hachimi_app_catalog_t * config);
const hachimi_app_descriptor_t * hachimi_app_home_list(size_t * count);
const hachimi_app_descriptor_t * hachimi_app_dock_list(size_t * count);
void hachimi_app_system_start(void);
void hachimi_app_system_stop(void);

#endif
