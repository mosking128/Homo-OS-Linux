#include "led.h"
#include "led_backend.h"
#include <stdio.h>
#include <string.h>

#define WRITE_INTERVAL_MS 40U
enum { JOB_NONE, JOB_READ, JOB_SET };

static bool start(hachimi_led_service_t * service, unsigned operation, uint32_t now)
{
    char payload[32];
    snprintf(payload, sizeof(payload), "%d\t%d\t%d", service->values[0], service->values[1], service->values[2]);
    if(!hachimi_job_start(&service->job, HACHIMI_HELPER_LED,
                         operation == JOB_READ ? "read" : "set",
                         operation == JOB_READ ? NULL : payload)) return false;
    service->operation = operation;
    if(operation == JOB_SET) service->last_write_ms = now;
    return true;
}

void hachimi_led_init(hachimi_led_service_t * service)
{
    memset(service, 0, sizeof(*service));
    hachimi_job_init(&service->job, service->output, sizeof(service->output));
    service->status = HACHIMI_LED_PREVIEW;
}

void hachimi_led_close(hachimi_led_service_t * service)
{
    hachimi_job_cancel(&service->job);
    service->operation = JOB_NONE;
    service->pending_write = service->direct = false;
}

void hachimi_led_open(hachimi_led_service_t * service, uint32_t now)
{
    hachimi_led_close(service);
    service->user_changed = false;
    service->last_write_ms = now - WRITE_INTERVAL_MS;
    if(!hachimi_system_services_supported()) { service->status = HACHIMI_LED_PREVIEW; return; }
    if(hachimi_led_backend_read(service->values)) {
        service->direct = true;
        service->status = HACHIMI_LED_DIRECT;
        return;
    }
    service->status = start(service, JOB_READ, now) ? HACHIMI_LED_READING : HACHIMI_LED_START_ERROR;
}

void hachimi_led_set(hachimi_led_service_t * service, unsigned channel, int value, uint32_t now)
{
    if(channel >= 3 || value < 0 || value > 255) return;
    service->values[channel] = value;
    service->user_changed = true;
    if(!hachimi_system_services_supported()) { service->status = HACHIMI_LED_PREVIEW; return; }
    if(service->direct) {
        if(hachimi_led_backend_write(channel, value)) { service->status = HACHIMI_LED_DIRECT; return; }
        service->direct = false;
        service->last_write_ms = now - WRITE_INTERVAL_MS;
    }
    service->pending_write = true;
    service->status = HACHIMI_LED_SETTING;
}

bool hachimi_led_poll(hachimi_led_service_t * service, uint32_t now)
{
    bool changed = false;
    if(service->job.running) {
        if(!hachimi_job_poll(&service->job)) return false;
        unsigned completed = service->operation;
        service->operation = JOB_NONE;
        changed = true;
        if(!service->job.success) {
            service->status = HACHIMI_LED_IO_ERROR;
            service->pending_write = false;
            return true;
        }
        if(completed == JOB_READ && !service->user_changed) {
            int values[3];
            if(sscanf(service->output, "RGB\t%d\t%d\t%d", &values[0], &values[1], &values[2]) == 3 &&
               values[0] >= 0 && values[0] <= 255 && values[1] >= 0 && values[1] <= 255 &&
               values[2] >= 0 && values[2] <= 255) {
                memcpy(service->values, values, sizeof(values));
                service->status = HACHIMI_LED_READY;
            }
            else service->status = HACHIMI_LED_PROTOCOL_ERROR;
        }
        else if(completed == JOB_SET) service->status = HACHIMI_LED_APPLIED;
    }
    if(service->pending_write && !service->job.running &&
       (uint32_t)(now - service->last_write_ms) >= WRITE_INTERVAL_MS) {
        service->pending_write = false;
        service->status = start(service, JOB_SET, now) ? HACHIMI_LED_SETTING : HACHIMI_LED_START_ERROR;
        changed = true;
    }
    return changed;
}
