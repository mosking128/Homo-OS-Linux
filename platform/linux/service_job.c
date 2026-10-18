#include "services/job.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static uint64_t monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

bool hachimi_system_services_supported(void) { return true; }

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
    if(job->running || job->capacity == 0 || operation == NULL ||
       strchr(operation, '\n') || (payload && strchr(payload, '\n'))) return false;
    const char * adapter;
    switch(helper) {
        case HACHIMI_HELPER_NETWORK: adapter = "/usr/local/libexec/hachimi-network"; break;
        case HACHIMI_HELPER_TIME: adapter = "/usr/local/libexec/hachimi-time"; break;
        case HACHIMI_HELPER_LED: adapter = "/usr/local/libexec/hachimi-led"; break;
        default: return false;
    }
    char input[1024];
    int length = snprintf(input, sizeof(input), "%s%s%s\n", operation,
                          payload ? "\t" : "", payload ? payload : "");
    if(length <= 0 || (size_t)length >= sizeof(input)) return false;
    int in[2], out[2];
    if(pipe(in) < 0) return false;
    if(pipe(out) < 0) { close(in[0]); close(in[1]); return false; }
    pid_t child = fork();
    if(child == 0) {
        dup2(in[0], STDIN_FILENO);
        dup2(out[1], STDOUT_FILENO);
        int null_fd = open("/dev/null", O_WRONLY);
        if(null_fd >= 0) { dup2(null_fd, STDERR_FILENO); close(null_fd); }
        close(in[0]); close(in[1]); close(out[0]); close(out[1]);
        execl("/usr/bin/sudo", "sudo", "-n", adapter, (char *)NULL);
        _exit(127);
    }
    close(in[0]); close(out[1]);
    if(child < 0) { close(in[1]); close(out[0]); return false; }
    struct sigaction ignored, previous;
    memset(&ignored, 0, sizeof(ignored));
    ignored.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &ignored, &previous);
    ssize_t sent;
    do { sent = write(in[1], input, (size_t)length); } while(sent < 0 && errno == EINTR);
    sigaction(SIGPIPE, &previous, NULL);
    memset(input, 0, sizeof(input));
    close(in[1]);
    job->process_id = child;
    job->output_fd = out[0];
    fcntl(out[0], F_SETFL, O_NONBLOCK);
    fcntl(out[0], F_SETFD, FD_CLOEXEC);
    job->length = 0;
    job->output[0] = '\0';
    job->running = true;
    job->success = false;
    job->truncated = false;
    job->deadline_ms = monotonic_ms() + 120000;
    if(sent != length) { hachimi_job_cancel(job); return false; }
    return true;
}

static void drain_output(hachimi_service_job_t * job)
{
    for(;;) {
        char buffer[2048];
        ssize_t length = read(job->output_fd, buffer, sizeof(buffer));
        if(length < 0 && errno == EINTR) continue;
        if(length <= 0) break;
        size_t room = job->capacity - job->length - 1;
        size_t copy = (size_t)length < room ? (size_t)length : room;
        memcpy(job->output + job->length, buffer, copy);
        job->length += copy;
        if(copy != (size_t)length) job->truncated = true;
    }
    job->output[job->length] = '\0';
}

bool hachimi_job_poll(hachimi_service_job_t * job)
{
    if(!job->running) return false;
    drain_output(job);
    int status = 0;
    pid_t result = waitpid(job->process_id, &status, WNOHANG);
    if(result == 0 && monotonic_ms() >= job->deadline_ms) {
        hachimi_job_cancel(job);
        return true;
    }
    if(result == 0 || (result < 0 && errno == EINTR)) return false;
    drain_output(job);
    job->success = result == job->process_id && WIFEXITED(status) &&
                   WEXITSTATUS(status) == 0 && !job->truncated;
    close(job->output_fd);
    job->output_fd = job->process_id = -1;
    job->running = false;
    return true;
}

void hachimi_job_cancel(hachimi_service_job_t * job)
{
    if(!job->running) return;
    kill(job->process_id, SIGKILL);
    while(waitpid(job->process_id, NULL, 0) < 0 && errno == EINTR) {}
    close(job->output_fd);
    job->output_fd = job->process_id = -1;
    job->running = job->success = false;
}
