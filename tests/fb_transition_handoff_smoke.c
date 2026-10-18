/* Exercise the real backend against a deferred LCD controller. In particular,
 * FBIOPAN_DISPLAY queues a page, and the first VSYNC notification is stale. */
#define _DEFAULT_SOURCE
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <linux/fb.h>
static int simulated_ioctl(int fd, unsigned long request, ...);
static int simulated_clock(clockid_t id, struct timespec * value);
static void * checked_copy(void * target, const void * source, size_t bytes);
#define ioctl simulated_ioctl
#define clock_gettime simulated_clock
#define memcpy checked_copy
#include "../platform/linux/fb_transition.c"
#undef ioctl
#undef clock_gettime
#undef memcpy

#define CHECK(c) do { if(!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static uint8_t pages[800*480*2*2], desktop[800*480*2], app[800*480*2];
static unsigned scan_page, queued_page, unsafe_writes;
static int stale_irq, checking;
static uint64_t now_us;
static int simulated_clock(clockid_t id, struct timespec * value)
{
    (void)id; value->tv_sec=now_us/1000000; value->tv_nsec=(now_us%1000000)*1000; return 0;
}
static int simulated_ioctl(int fd, unsigned long request, ...)
{
    (void)fd;
    va_list args;va_start(args,request);void * argument=va_arg(args,void *);va_end(args);
    if(request==FBIOGET_VSCREENINFO) {
        struct fb_var_screeninfo * v=argument;memset(v,0,sizeof(*v));v->yoffset=scan_page*480;return 0;
    }
    if(request==FBIOPAN_DISPLAY) {
        struct fb_var_screeninfo * v=argument;queued_page=v->yoffset/480;stale_irq=1;return 0;
    }
    if(request==FBIO_WAITFORVSYNC) {
        if(stale_irq) { stale_irq=0;now_us+=100; }
        else { now_us+=17000;scan_page=queued_page; }
        return 0;
    }
    CHECK(0);return -1;
}
static void * checked_copy(void * target, const void * source, size_t bytes)
{
    uintptr_t first=(uintptr_t)pages+scan_page*sizeof(desktop),last=first+sizeof(desktop);
    uintptr_t begin=(uintptr_t)target;
    if(checking && begin<last && begin+bytes>first) unsafe_writes++;
    return memmove(target,source,bytes);
}
int main(void)
{
    lv_init();
    lv_display_t * display=lv_display_create(800,480);CHECK(display);
    lv_draw_buf_t background={0},content={0};
    background.header.cf=content.header.cf=LV_COLOR_FORMAT_RGB565;
    background.header.w=content.header.w=800;background.header.h=content.header.h=480;
    background.header.stride=content.header.stride=1600;
    background.data=desktop;content.data=app;
    memset(desktop,0x7b,sizeof(desktop));memset(app,0x12,sizeof(app));
    for(unsigned frames=1;frames<=2;frames++) {
        memset(&state,0,sizeof(state));state.fd=1;state.display=display;state.pixels=pages;
        state.background=malloc(sizeof(desktop));CHECK(state.background);
        state.page_size=sizeof(desktop);state.stride=1600;state.width=800;state.height=480;
        state.double_buffered=1;now_us=1000000;scan_page=queued_page=unsafe_writes=0;
        stale_irq=0;checking=0;memcpy(pages,app,sizeof(app));checking=1;
        CHECK(direct_begin(NULL,&background,&content,0,0,800,480,0));
        CHECK(scan_page==1);CHECK(!unsafe_writes);
        /* The desktop is copied only after scanout has moved to page 1. */
        CHECK(!memcmp(pages,desktop,sizeof(desktop)));
        for(unsigned i=0;i<frames;i++) direct_draw(NULL,50,40,400,240,20,i+1==frames);
        direct_end(NULL);CHECK(scan_page==1);CHECK(!unsafe_writes);
        checked_copy(pages,desktop,sizeof(desktop)); /* LVGL's target render */
        direct_present(NULL);CHECK(scan_page==0);CHECK(!unsafe_writes);
        CHECK(!memcmp(pages,desktop,sizeof(desktop)));
        checking=0;free(state.background);
    }
    puts("PASS delayed page flips and stale VSYNC never copy over scanout during begin/end/present (both final-page parities)");
    return 0;
}
