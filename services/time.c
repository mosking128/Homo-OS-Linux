#include "time.h"
#include <stdio.h>
#include <string.h>

void hachimi_time_init(hachimi_time_service_t * service)
{
    memset(service, 0, sizeof(*service));
    hachimi_job_init(&service->job, service->output, sizeof(service->output));
}

bool hachimi_time_request(hachimi_time_service_t * service, const char * operation, const char * timezone)
{
    if(strcmp(operation, "status") && strcmp(operation, "zones") && strcmp(operation, "ntp-on") &&
       strcmp(operation, "ntp-off") && strcmp(operation, "zone-set")) return false;
    if(strcmp(operation, "zone-set") == 0 && (timezone == NULL || strlen(timezone) >= 128 ||
       strchr(timezone, '\n') || strchr(timezone, '\t'))) return false;
    if(!hachimi_job_start(&service->job, HACHIMI_HELPER_TIME, operation, timezone)) return false;
    snprintf(service->operation, sizeof(service->operation), "%s", operation);
    return true;
}

static bool parse_status(hachimi_time_service_t * service)
{
    hachimi_time_state_t state = {0};
    char ntp[16] = {0}, synced[16] = {0};
    int count = sscanf(service->output, "TIME\t%4d-%2d-%2d\t%15[^\t]\t%127[^\t]\t%15[^\t]\t%15[^\n]",
                       &state.year, &state.month, &state.day, state.clock, state.timezone, ntp, synced);
    if(count != 7 || (strcmp(ntp, "0") && strcmp(ntp, "1"))) return false;
    state.ntp_enabled = strcmp(ntp, "1") == 0;
    state.synchronized = strcmp(synced, "1") == 0;
    service->state = state;
    return true;
}

static bool parse_zones(hachimi_time_service_t * service)
{
    size_t used = 0;
    char * line = service->output;
    while(*line) {
        char * end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        if(length > 5 && strncmp(line, "ZONE\t", 5) == 0 && length - 5 < 128) {
            if(used + length - 5 + 2 >= sizeof(service->zones)) return false;
            memcpy(service->zones + used, line + 5, length - 5);
            used += length - 5;
            service->zones[used++] = '\n';
        }
        if(end == NULL) break;
        line = end + 1;
    }
    if(used == 0) return false;
    service->zones[used - 1] = '\0';
    return true;
}

bool hachimi_time_poll(hachimi_time_service_t * service)
{
    if(!hachimi_job_poll(&service->job)) return false;
    service->succeeded = service->job.success && strncmp(service->output, "ERROR\t", 6) != 0;
    if(service->succeeded && strcmp(service->operation, "status") == 0) service->succeeded = parse_status(service);
    if(service->succeeded && strcmp(service->operation, "zones") == 0) service->succeeded = parse_zones(service);
    return true;
}
void hachimi_time_cancel(hachimi_time_service_t * service) { hachimi_job_cancel(&service->job); }
