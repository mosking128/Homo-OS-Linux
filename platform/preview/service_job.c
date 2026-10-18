/* Non-mutating backend shared by desktop previews and headless UI tests. */
#include "services/job.h"
#include <string.h>

bool hachimi_system_services_supported(void) { return false; }
void hachimi_job_init(hachimi_service_job_t * job, char * output, size_t capacity)
{
    memset(job, 0, sizeof(*job));
    job->process_id = job->output_fd = -1;
    job->output = output;
    job->capacity = capacity;
    if(capacity) output[0] = '\0';
}
bool hachimi_job_start(hachimi_service_job_t * job, hachimi_helper_t helper,
                       const char * operation, const char * payload)
{
    (void)job; (void)helper; (void)operation; (void)payload;
    return false;
}
bool hachimi_job_poll(hachimi_service_job_t * job) { (void)job; return false; }
void hachimi_job_cancel(hachimi_service_job_t * job) { job->running = job->success = false; }
