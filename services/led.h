#ifndef HACHIMI_LED_H
#define HACHIMI_LED_H
#include "job.h"

typedef enum {
    HACHIMI_LED_PREVIEW, HACHIMI_LED_READING, HACHIMI_LED_DIRECT,
    HACHIMI_LED_READY, HACHIMI_LED_SETTING, HACHIMI_LED_APPLIED,
    HACHIMI_LED_IO_ERROR, HACHIMI_LED_PROTOCOL_ERROR, HACHIMI_LED_START_ERROR
} hachimi_led_status_t;

/* Owned by one consumer; all calls run on its event-loop thread. */
typedef struct {
    hachimi_service_job_t job;
    char output[128];
    int values[3];
    uint32_t last_write_ms;
    bool pending_write, user_changed, direct;
    unsigned operation;
    hachimi_led_status_t status;
} hachimi_led_service_t;

void hachimi_led_init(hachimi_led_service_t * service);
void hachimi_led_open(hachimi_led_service_t * service, uint32_t now_ms);
void hachimi_led_set(hachimi_led_service_t * service, unsigned channel, int value, uint32_t now_ms);
bool hachimi_led_poll(hachimi_led_service_t * service, uint32_t now_ms);
void hachimi_led_close(hachimi_led_service_t * service);
#endif
