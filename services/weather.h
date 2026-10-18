#ifndef HACHIMI_WEATHER_H
#define HACHIMI_WEATHER_H
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
typedef struct {
    char date[16], day[48], night[48], wind[48], speed[16], scale[16];
    int code, low, high;
} hachimi_weather_day_t;
typedef struct {
    char city[64], location[128], text[48], updated[48];
    int temperature, code;
    bool now_valid;
    unsigned days;
    hachimi_weather_day_t daily[3];
    char brief[6][64], details[6][384];
    char life_names[6][24];
    time_t fetched[3];
    char errors[3][128];
} hachimi_weather_t;
/* Private key is read from HACHIMI_WEATHER_KEY or weather.key in the
 * private state directory. Never compiled into the executable or UI. */
const char * hachimi_weather_location(void);
bool hachimi_weather_select(const char * location);
const hachimi_weather_t * hachimi_weather_data(void);
bool hachimi_weather_refresh(void);
bool hachimi_weather_busy(void);
bool hachimi_weather_poll(void);
void hachimi_weather_shutdown(void);
/* Bounded decoder, also exercised by the regression checks. */
bool hachimi_weather_decode(hachimi_weather_t * out, unsigned endpoint,
                             const char * json);
#endif
