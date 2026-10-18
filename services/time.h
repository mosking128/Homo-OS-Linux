#ifndef HACHIMI_TIME_SERVICE_H
#define HACHIMI_TIME_SERVICE_H
#include "job.h"
typedef struct {
    int year, month, day;
    char clock[16], timezone[128];
    bool ntp_enabled, synchronized;
} hachimi_time_state_t;
typedef struct {
    hachimi_service_job_t job;
    hachimi_time_state_t state;
    char output[32768], zones[32768], operation[24];
    bool succeeded;
} hachimi_time_service_t;
void hachimi_time_init(hachimi_time_service_t * service);
bool hachimi_time_request(hachimi_time_service_t * service, const char * operation, const char * timezone);
bool hachimi_time_poll(hachimi_time_service_t * service);
void hachimi_time_cancel(hachimi_time_service_t * service);
#endif
