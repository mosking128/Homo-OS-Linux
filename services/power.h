#ifndef HACHIMI_POWER_H
#define HACHIMI_POWER_H
#include <stdbool.h>
typedef struct {
    bool battery_present, external_power, charging;
    int percent; /* -1 when the platform cannot report capacity. */
} hachimi_power_state_t;
bool hachimi_power_read(hachimi_power_state_t * state);
#endif
