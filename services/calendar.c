#include "calendar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

int hachimi_date_month_days(int year, int month)
{
    static const int lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if(month < 1 || month > 12) return 0;
    return lengths[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}
bool hachimi_date_valid(hachimi_date_t date)
{
    return date.year >= 1900 && date.year <= 2199 && date.day >= 1 &&
           date.day <= hachimi_date_month_days(date.year, date.month);
}
int hachimi_date_weekday(hachimi_date_t date)
{
    static const int offsets[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    int year = date.year - (date.month < 3);
    if(!hachimi_date_valid(date)) return 0;
    return (year + year / 4 - year / 100 + year / 400 + offsets[date.month - 1] + date.day) % 7;
}
int hachimi_date_compare(hachimi_date_t a, hachimi_date_t b)
{
    int first = a.year * 10000 + a.month * 100 + a.day;
    int second = b.year * 10000 + b.month * 100 + b.day;
    return (first > second) - (first < second);
}
hachimi_date_t hachimi_date_add_days(hachimi_date_t date, int delta)
{
    if(!hachimi_date_valid(date)) return date;
    while(delta > 0) {
        if(date.year == 2199 && date.month == 12 && date.day == 31) break;
        if(++date.day > hachimi_date_month_days(date.year, date.month)) {
            date.day = 1;
            if(++date.month > 12) { date.month = 1; date.year++; }
        }
        delta--;
    }
    while(delta < 0) {
        if(date.year == 1900 && date.month == 1 && date.day == 1) break;
        if(--date.day < 1) {
            if(--date.month < 1) { date.month = 12; date.year--; }
            date.day = hachimi_date_month_days(date.year, date.month);
        }
        delta++;
    }
    return date;
}
static void encode(const char * input, char * output)
{
    static const char hex[] = "0123456789abcdef";
    while(*input) { unsigned c = (unsigned char)*input++; *output++ = hex[c >> 4]; *output++ = hex[c & 15]; }
    *output = '\0';
}
static int unhex(char c)
{
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
static bool decode(const char * input, char * output, size_t capacity)
{
    size_t length = strlen(input);
    if(length % 2 != 0 || length / 2 >= capacity) return false;
    for(size_t i = 0; i < length; i += 2) {
        int a = unhex(input[i]), b = unhex(input[i + 1]);
        if(a < 0 || b < 0 || (a == 0 && b == 0)) return false;
        output[i / 2] = (char)(a * 16 + b);
    }
    output[length / 2] = '\0';
    return true;
}
static bool valid_event(const hachimi_calendar_event_t * event)
{
    return hachimi_date_valid(event->date) && event->category < 3 &&
           event->start_minute < 1440 && event->end_minute <= 1440 &&
           event->end_minute > event->start_minute && event->title[0] != '\0' &&
           memchr(event->title, '\0', sizeof(event->title)) != NULL &&
           memchr(event->location, '\0', sizeof(event->location)) != NULL;
}
bool hachimi_calendar_load(hachimi_calendar_t * calendar)
{
    memset(calendar, 0, sizeof(*calendar)); calendar->next_id = 1;
    char home[HACHIMI_FS_PATH_MAX], directory[HACHIMI_FS_PATH_MAX];
    if(!hachimi_fs_home(home, sizeof(home)) ||
       !hachimi_fs_join(home, ".hachimi", directory, sizeof(directory))) return false;
    if(!hachimi_fs_exists(directory) && !hachimi_fs_mkdir(directory)) return false;
    if(!hachimi_fs_is_directory(directory) ||
       !hachimi_fs_join(directory, "calendar-events.txt", calendar->path, sizeof(calendar->path))) return false;
    if(hachimi_fs_is_link(calendar->path)) return false;
    FILE * file = fopen(calendar->path, "rb");
    if(file == NULL) {
        calendar->writable = !hachimi_fs_exists(calendar->path);
        return calendar->writable;
    }
    char line[640]; bool valid = true;
    while(fgets(line, sizeof(line), file) != NULL) {
        if(calendar->count >= HACHIMI_CALENDAR_MAX_EVENTS) { valid = false; break; }
        hachimi_calendar_event_t item = {0};
        unsigned all_day; char title[194], location[194];
        /* '-' represents an empty location, keeping the record unambiguous. */
        if(sscanf(line, "%u|%d|%d|%d|%u|%u|%u|%u|%193[^|]|%193[^\r\n]",
                  &item.id, &item.date.year, &item.date.month, &item.date.day,
                  &item.start_minute, &item.end_minute, &all_day, &item.category, title, location) != 10 ||
           item.id == 0 || item.id == UINT32_MAX || all_day > 1 ||
           !decode(title, item.title, sizeof(item.title)) ||
           (strcmp(location, "-") != 0 && !decode(location, item.location, sizeof(item.location)))) {
            valid = false; break;
        }
        item.all_day = all_day != 0;
        if(!valid_event(&item)) { valid = false; break; }
        for(unsigned i = 0; i < calendar->count; i++) if(calendar->events[i].id == item.id) valid = false;
        if(!valid) break;
        calendar->events[calendar->count++] = item;
        if(item.id >= calendar->next_id) calendar->next_id = item.id + 1;
    }
    if(ferror(file)) valid = false;
    fclose(file);
    calendar->writable = valid;
    return valid;
}
static bool save(hachimi_calendar_t * calendar)
{
    if(!calendar->writable || calendar->path[0] == '\0' || hachimi_fs_is_link(calendar->path)) return false;
    char temporary[HACHIMI_FS_PATH_MAX];
    int size = snprintf(temporary, sizeof(temporary), "%s.tmp", calendar->path);
    if(size < 0 || (size_t)size >= sizeof(temporary) || hachimi_fs_is_link(temporary)) return false;
    /* Filesystem service rejects symlinks. Close the completed file before the
     * atomic replacement, preserving the previous calendar if writing fails. */
    char text[HACHIMI_CALENDAR_MAX_EVENTS * 512]; size_t used = 0; text[0] = '\0';
    for(unsigned i = 0; i < calendar->count; i++) {
        const hachimi_calendar_event_t * event = &calendar->events[i];
        char title[194], location[194]; encode(event->title, title); encode(event->location, location);
        int written = snprintf(text + used, sizeof(text) - used, "%u|%d|%d|%d|%u|%u|%u|%u|%s|%s\n",
                               event->id, event->date.year, event->date.month, event->date.day,
                               event->start_minute, event->end_minute, event->all_day ? 1u : 0u,
                               event->category, title, location[0] ? location : "-");
        if(written < 0 || (size_t)written >= sizeof(text) - used) return false;
        used += (size_t)written;
    }
    if(!hachimi_fs_write_text(temporary, text, !hachimi_fs_exists(temporary))) return false;
#ifdef _WIN32
    return MoveFileExA(temporary, calendar->path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(temporary, calendar->path) == 0;
#endif
}
bool hachimi_calendar_put(hachimi_calendar_t * calendar, hachimi_calendar_event_t * event)
{
    if(!valid_event(event) || !calendar->writable) return false;
    unsigned index = 0;
    while(index < calendar->count && calendar->events[index].id != event->id) index++;
    bool adding = event->id == 0;
    if((adding && (calendar->count >= HACHIMI_CALENDAR_MAX_EVENTS || calendar->next_id == UINT32_MAX)) ||
       (!adding && index == calendar->count)) return false;
    if(adding) index = calendar->count++;
    hachimi_calendar_event_t previous = calendar->events[index];
    hachimi_calendar_event_t updated = *event;
    if(adding) updated.id = calendar->next_id;
    calendar->events[index] = updated;
    if(!save(calendar)) {
        calendar->events[index] = previous;
        if(adding) calendar->count--;
        return false;
    }
    if(adding) calendar->next_id++;
    *event = updated;
    return true;
}
bool hachimi_calendar_delete(hachimi_calendar_t * calendar, uint32_t id)
{
    if(!calendar->writable) return false;
    unsigned index = 0;
    while(index < calendar->count && calendar->events[index].id != id) index++;
    if(index == calendar->count) return false;
    hachimi_calendar_event_t previous = calendar->events[index];
    memmove(calendar->events + index, calendar->events + index + 1,
            (calendar->count - index - 1) * sizeof(calendar->events[0]));
    calendar->count--;
    if(save(calendar)) return true;
    memmove(calendar->events + index + 1, calendar->events + index,
            (calendar->count - index) * sizeof(calendar->events[0]));
    calendar->events[index] = previous; calendar->count++;
    return false;
}
