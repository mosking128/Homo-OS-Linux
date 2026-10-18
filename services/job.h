#ifndef HACHIMI_SERVICE_JOB_H
#define HACHIMI_SERVICE_JOB_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { HACHIMI_HELPER_NETWORK, HACHIMI_HELPER_TIME, HACHIMI_HELPER_LED } hachimi_helper_t;
/* Platform-owned transport. Service modules own the supplied output storage. */
typedef struct {
    int process_id;
    int output_fd;
    char * output;
    size_t capacity;
    size_t length;
    bool running;
    bool success;
    bool truncated;
    uint64_t deadline_ms;
} hachimi_service_job_t;

bool hachimi_system_services_supported(void);
void hachimi_job_init(hachimi_service_job_t * job, char * output, size_t capacity);
bool hachimi_job_start(hachimi_service_job_t * job, hachimi_helper_t helper,
                       const char * operation, const char * payload);
/* Returns true exactly once when a running operation completes. */
bool hachimi_job_poll(hachimi_service_job_t * job);
void hachimi_job_cancel(hachimi_service_job_t * job);
#endif
