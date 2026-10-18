#ifndef HACHIMI_CALENDAR_SERVICE_H
#define HACHIMI_CALENDAR_SERVICE_H
#include <stdbool.h>
#include <stdint.h>
#include "filesystem.h"

#define HACHIMI_CALENDAR_MAX_EVENTS 64
typedef struct { int year, month, day; } hachimi_date_t;
typedef struct {
    uint32_t id;
    hachimi_date_t date;
    unsigned start_minute, end_minute;
    bool all_day;
    unsigned category;
    char title[97];
    char location[97];
} hachimi_calendar_event_t;
typedef struct {
    hachimi_calendar_event_t events[HACHIMI_CALENDAR_MAX_EVENTS];
    unsigned count;
    uint32_t next_id;
    char path[HACHIMI_FS_PATH_MAX];
    bool writable;
} hachimi_calendar_t;

int hachimi_date_month_days(int year, int month);
bool hachimi_date_valid(hachimi_date_t date);
int hachimi_date_weekday(hachimi_date_t date); /* Sunday=0 */
int hachimi_date_compare(hachimi_date_t a, hachimi_date_t b);
hachimi_date_t hachimi_date_add_days(hachimi_date_t date, int delta);
bool hachimi_calendar_load(hachimi_calendar_t * calendar);
bool hachimi_calendar_put(hachimi_calendar_t * calendar, hachimi_calendar_event_t * event);
bool hachimi_calendar_delete(hachimi_calendar_t * calendar, uint32_t id);
#endif
