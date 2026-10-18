#define _GNU_SOURCE
#include "pxp_engine.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
/* Keep BSP's bool typedef out of translation units which include LVGL. */
#include <linux/pxp_device.h>

struct hachimi_pxp {
    int fd, socket, usable;
    pid_t worker;
    unsigned width, height, channel;
    struct pxp_mem_desc source, scratch;
    void * pixels;
    void * output;
};

typedef struct {
    uint32_t x, y, width, height, destination, stride, output_width, output_height;
} blit_request_t;

static int64_t milliseconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int perform(hachimi_pxp_t * p, const blit_request_t * r)
{
    struct pxp_mem_flush clean = {.handle=p->source.handle, .type=CACHE_CLEAN};
    struct pxp_config_data c = {0};
    struct pxp_chan_handle done = {.handle=p->channel};
    c.handle = p->channel;
    c.layer_nr = 2;
    c.s0_param.width = p->width;
    c.s0_param.height = p->height;
    c.s0_param.stride = p->width * 2;
    c.s0_param.pixel_fmt = PXP_PIX_FMT_RGB565;
    c.s0_param.paddr = p->source.phys_addr;
    c.out_param.width = r->output_width;
    c.out_param.height = r->output_height;
    c.out_param.stride = r->stride;
    c.out_param.pixel_fmt = PXP_PIX_FMT_RGB565;
    c.out_param.paddr = r->destination;
    c.proc_data.srect = (struct rect){.left=r->x, .top=r->y, .width=r->width, .height=r->height};
    c.proc_data.drect = (struct rect){.width=r->output_width, .height=r->output_height};
    c.proc_data.scaling = r->width != r->output_width || r->height != r->output_height;
    c.proc_data.op_type = PXP_OP_2D;
    c.proc_data.working_mode = PXP_MODE_LEGACY;
    /* fill_en deliberately stays zero: input-store fill stalls this BSP. */
    if(ioctl(p->fd, PXP_IOC_FLUSH_PHYMEM, &clean) < 0 ||
       ioctl(p->fd, PXP_IOC_CONFIG_CHAN, &c) < 0 ||
       ioctl(p->fd, PXP_IOC_START_CHAN, &c.handle) < 0 ||
       ioctl(p->fd, PXP_IOC_WAIT4CMPLT, &done) < 0) return -errno;
    return 0;
}

static void worker_loop(hachimi_pxp_t * p)
{
    prctl(PR_SET_NAME, "hachimi-pxp");
    /* The parent drains/releases its mappings first during a service stop. */
    signal(SIGTERM, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if(getppid() == 1) _exit(0);
    blit_request_t request;
    while(recv(p->socket, &request, sizeof(request), 0) == sizeof(request)) {
        int result = perform(p, &request);
        if(send(p->socket, &result, sizeof(result), MSG_NOSIGNAL) != sizeof(result)) break;
    }
    /* The final fd release can block in this old kernel following a DMA fault.
     * It belongs to this isolated worker, never to the desktop's UI thread. */
    close(p->fd);
    _exit(0);
}

static int allocate(hachimi_pxp_t * p, struct pxp_mem_desc * mem, unsigned type, void ** address)
{
    mem->size = p->width * p->height * 2;
    mem->mtype = type;
    if(ioctl(p->fd, PXP_IOC_GET_PHYMEM, mem) < 0) return -1;
    *address = mmap(NULL, mem->size, PROT_READ | PROT_WRITE, MAP_SHARED, p->fd, mem->phys_addr);
    return *address == MAP_FAILED ? -1 : 0;
}

hachimi_pxp_t * hachimi_pxp_open(const char * device, unsigned width, unsigned height)
{
    if(!width || !height || width > 2048 || height > 2048) { errno=EINVAL; return NULL; }
    hachimi_pxp_t * p = calloc(1, sizeof(*p));
    if(!p) return NULL;
    p->fd = p->socket = -1;
    p->pixels = p->output = MAP_FAILED;
    p->width = width;
    p->height = height;
    p->fd = open(device, O_RDWR | O_CLOEXEC);
    if(p->fd < 0) goto fail;
    if(ioctl(p->fd, PXP_IOC_GET_CHAN, &p->channel) < 0) goto fail;
    if(allocate(p, &p->source, MEMORY_TYPE_CACHED, &p->pixels) < 0 ||
       allocate(p, &p->scratch, MEMORY_TYPE_WC, &p->output) < 0) goto fail;
    int sockets[2];
    if(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) < 0) goto fail;
    p->worker = fork();
    if(p->worker < 0) { close(sockets[0]); close(sockets[1]); goto fail; }
    if(p->worker == 0) {
        close(sockets[0]);
        p->socket = sockets[1];
        worker_loop(p);
    }
    close(sockets[1]);
    p->socket = sockets[0];
    p->usable = 1;
    return p;
fail: {
        int error = errno;
        hachimi_pxp_close(p);
        errno = error;
        return NULL;
    }
}

void hachimi_pxp_close(hachimi_pxp_t * p)
{
    if(!p) return;
    /* VMAs also reference the device file. Drop every parent reference while
     * the worker still owns its fd; only the worker may do the final release. */
    if(p->pixels != MAP_FAILED) munmap(p->pixels, p->source.size);
    if(p->output != MAP_FAILED) munmap(p->output, p->scratch.size);
    if(p->worker > 0) {
        close(p->fd);
        p->fd = -1;
        if(p->socket >= 0) close(p->socket);
        if(!p->usable) kill(p->worker, SIGKILL);
        int64_t deadline = milliseconds() + 100;
        while(waitpid(p->worker, NULL, WNOHANG) == 0) {
            if(milliseconds() >= deadline) { kill(p->worker, SIGKILL); break; }
            usleep(1000);
        }
        (void)waitpid(p->worker, NULL, WNOHANG);
    }
    if(p->fd >= 0) close(p->fd);
    free(p);
}

void * hachimi_pxp_pixels(hachimi_pxp_t * p) { return p->pixels; }
void * hachimi_pxp_scratch(hachimi_pxp_t * p) { return p->output; }
uint32_t hachimi_pxp_scratch_address(hachimi_pxp_t * p) { return p->scratch.phys_addr; }
int hachimi_pxp_worker_id(hachimi_pxp_t * p) { return p->worker; }

int hachimi_pxp_blit(hachimi_pxp_t * p, unsigned x, unsigned y,
                     unsigned width, unsigned height, uint32_t destination,
                     unsigned stride, unsigned output_width, unsigned output_height)
{
    if(!p || !p->usable) { errno=ENODEV; return -1; }
    if(!width || !height || x >= p->width || y >= p->height ||
       width > p->width-x || height > p->height-y || !destination ||
       !output_width || output_width > p->width || !output_height || output_height > p->height ||
       stride < output_width*2 || stride > 8192 || (stride & 1)) { errno=EINVAL; return -1; }
    blit_request_t r = {x,y,width,height,destination,stride,output_width,output_height};
    int result;
    if(send(p->socket, &r, sizeof(r), MSG_NOSIGNAL) != sizeof(r)) goto broken;
    int64_t deadline = milliseconds() + 100;
    for(;;) {
        int remaining = (int)(deadline-milliseconds());
        struct pollfd fd = {.fd=p->socket, .events=POLLIN};
        if(remaining <= 0) { errno=ETIMEDOUT; goto broken; }
        int ready = poll(&fd, 1, remaining);
        if(ready < 0 && errno == EINTR) continue;
        if(!ready) { errno=ETIMEDOUT; goto broken; }
        if(ready < 0) goto broken;
        if(recv(p->socket, &result, sizeof(result), MSG_DONTWAIT) != sizeof(result)) goto broken;
        if(result) { errno=-result; goto broken; }
        return 0;
    }
broken:
    p->usable = 0;
    /* Keep the worker's file references until close() has dropped ours. */
    return -1;
}
