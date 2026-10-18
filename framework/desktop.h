#ifndef HACHIMI_IOS_DESKTOP_H
#define HACHIMI_IOS_DESKTOP_H
#include "app_registry.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Default-product compatibility wrapper; implemented in product/default_apps.c. */
void ios_desktop_create(void);
bool hachimi_desktop_create(const hachimi_app_catalog_t * catalog);
bool hachimi_desktop_launch(hachimi_app_id_t id);
bool hachimi_desktop_home(void);
void hachimi_desktop_destroy(void);
void hachimi_desktop_set_app_gestures_enabled(bool enabled);

typedef struct {
    void * user_data;
    /* Borrowed RGB565 buffers remain valid until end(). content is optional;
     * NULL requests the lightweight launch panel / snapshot-failure fallback. */
    bool (*begin)(void * user_data, const lv_draw_buf_t * background,
                  const lv_draw_buf_t * content,
                  int32_t x, int32_t y, int32_t width, int32_t height, int32_t radius);
    void (*draw)(void * user_data, int32_t x, int32_t y, int32_t width, int32_t height,
                 int32_t radius, bool reveal_target);
    void (*end)(void * user_data);
    void (*present)(void * user_data);
} hachimi_desktop_transition_backend_t;

void hachimi_desktop_set_transition_backend(const hachimi_desktop_transition_backend_t * backend);

#ifdef __cplusplus
}
#endif

#endif
