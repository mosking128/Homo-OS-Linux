#include "services/led.h"
#include "services/led_backend.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool supported, direct_read, direct_write, complete, success;
static unsigned starts, cancels;
static char reply[128], operation[16], payload[32];
bool hachimi_system_services_supported(void) { return supported; }
bool hachimi_led_backend_read(int values[3])
{ values[0]=10; values[1]=20; values[2]=30; return direct_read; }
bool hachimi_led_backend_write(unsigned channel, int value)
{ assert(channel < 3 && value >= 0 && value <= 255); return direct_write; }
void hachimi_job_init(hachimi_service_job_t * job, char * output, size_t capacity)
{ memset(job,0,sizeof(*job)); job->output=output; job->capacity=capacity; }
bool hachimi_job_start(hachimi_service_job_t * job, hachimi_helper_t helper,
                       const char * op, const char * data)
{
    assert(helper == HACHIMI_HELPER_LED && !job->running);
    snprintf(operation,sizeof(operation),"%s",op);
    snprintf(payload,sizeof(payload),"%s",data ? data : "");
    starts++; job->running=true; return true;
}
bool hachimi_job_poll(hachimi_service_job_t * job)
{
    if(!complete) return false;
    complete=false; job->running=false; job->success=success;
    snprintf(job->output,job->capacity,"%s",reply); return true;
}
void hachimi_job_cancel(hachimi_service_job_t * job)
{ if(job->running) cancels++; job->running=false; }

int main(void)
{
    hachimi_led_service_t led;
    hachimi_led_init(&led); hachimi_led_open(&led,0);
    hachimi_led_set(&led,0,255,0);
    assert(led.status==HACHIMI_LED_PREVIEW && !led.pending_write && starts==0);
    supported=direct_read=direct_write=true;
    hachimi_led_open(&led,0); assert(led.direct && led.values[1]==20);
    hachimi_led_set(&led,0,42,1); assert(starts==0 && led.values[0]==42);
    direct_write=false;
    hachimi_led_set(&led,1,123,2); assert(!led.direct && led.pending_write);
    assert(hachimi_led_poll(&led,2) && starts==1 && !strcmp(payload,"42\t123\t30"));
    hachimi_led_set(&led,2,99,3); assert(!hachimi_led_poll(&led,3));
    success=complete=true; strcpy(reply,"OK");
    assert(hachimi_led_poll(&led,10) && starts==1);
    assert(hachimi_led_poll(&led,42) && starts==2 && !strcmp(payload,"42\t123\t99"));
    hachimi_led_close(&led); assert(cancels==1 && !led.job.running);
    direct_read=false;
    hachimi_led_open(&led,50); assert(!strcmp(operation,"read"));
    hachimi_led_set(&led,0,200,51);
    strcpy(reply,"RGB\t1\t2\t3"); complete=true;
    assert(hachimi_led_poll(&led,52) && led.values[0]==200);
    hachimi_led_close(&led); hachimi_led_open(&led,60);
    strcpy(reply,"RGB\t999\t2\t3"); complete=true;
    assert(hachimi_led_poll(&led,61) && led.status==HACHIMI_LED_PROTOCOL_ERROR);
    hachimi_led_open(&led,70); success=false; complete=true;
    assert(hachimi_led_poll(&led,71) && led.status==HACHIMI_LED_IO_ERROR);
    hachimi_led_close(&led);
    puts("PASS LED service: preview, direct write, helper fallback, coalescing, stale reads, malformed replies, failure and cancellation");
    return 0;
}
