#include "power.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <stdlib.h>
static bool read_value(const char * device, const char * field, char * value, size_t size)
{
    char path[512];
    if(snprintf(path, sizeof(path), "/sys/class/power_supply/%s/%s", device, field) >= (int)sizeof(path)) return false;
    FILE * file = fopen(path, "r");
    if(file == NULL) return false;
    bool ok = fgets(value, (int)size, file) != NULL;
    fclose(file);
    if(ok) value[strcspn(value, "\r\n")] = '\0';
    return ok;
}
#endif
bool hachimi_power_read(hachimi_power_state_t * state)
{
    memset(state, 0, sizeof(*state)); state->percent = -1;
#ifdef _WIN32
    SYSTEM_POWER_STATUS status;
    if(!GetSystemPowerStatus(&status)) return false;
    state->external_power = status.ACLineStatus == 1;
    state->battery_present = status.BatteryFlag != 255 && !(status.BatteryFlag & 128);
    state->charging = state->battery_present && (status.BatteryFlag & 8);
    if(state->battery_present && status.BatteryLifePercent <= 100) state->percent = status.BatteryLifePercent;
    return true;
#else
    DIR * directory = opendir("/sys/class/power_supply");
    if(directory == NULL) return false;
    struct dirent * entry;
    while((entry = readdir(directory)) != NULL) {
        char type[40], value[40];
        if(entry->d_name[0] == '.' || !read_value(entry->d_name, "type", type, sizeof(type))) continue;
        if(!strcmp(type, "Battery")) {
            if(read_value(entry->d_name, "present", value, sizeof(value)) && atoi(value) == 0) continue;
            state->battery_present = true;
            if(read_value(entry->d_name, "capacity", value, sizeof(value))) {
                char * end; long capacity = strtol(value, &end, 10);
                if(end != value && *end == '\0' && capacity >= 0 && capacity <= 100) state->percent = (int)capacity;
            }
            if(read_value(entry->d_name, "status", value, sizeof(value))) {
                if(!strcmp(value, "Charging")) state->charging = state->external_power = true;
                if(!strcmp(value, "Full")) state->external_power = true;
            }
        }
        else if(read_value(entry->d_name, "online", value, sizeof(value)) && atoi(value) == 1)
            state->external_power = true;
    }
    closedir(directory);
    /* The board is externally powered and normally has no battery device. */
    if(!state->battery_present) state->external_power = true;
    return true;
#endif
}
