#define _DEFAULT_SOURCE
#include "pxp_display.h"
#include "pxp_engine.h"
#include "display/lv_display_private.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* One physical display on this target. Keep LVGL's framebuffer driver as the
 * fallback; no changes to the vendored LVGL or Windows simulator are needed. */
static struct {
    lv_display_t * display;
    lv_display_flush_cb_t software_flush;
    hachimi_pxp_t * engine;
    void * original_buffer;
    uint32_t destination, stride, width, height;
    unsigned long hardware_jobs, software_jobs;
    int enabled;
} state;

static void flush(lv_display_t * display, const lv_area_t * area, uint8_t * pixels)
{
    int width = area->x2-area->x1+1, height = area->y2-area->y1+1;
    /* Avoid setup and cache-maintenance overhead on small updates. */
    if(state.enabled && pixels == hachimi_pxp_pixels(state.engine) &&
       lv_display_get_rotation(display) == LV_DISPLAY_ROTATION_0 &&
       area->x1 >= 0 && area->y1 >= 0 && area->x2 < (int)state.width && area->y2 < (int)state.height &&
       width > 0 && height > 0 && width*height >= 65536) {
        uint32_t destination = state.destination + area->y1*state.stride + area->x1*2;
        if(hachimi_pxp_blit(state.engine, area->x1, area->y1, width, height,
                           destination, state.stride, width, height) == 0) {
            state.hardware_jobs++;
            lv_display_flush_ready(display);
            return;
        }
        fprintf(stderr,"Hachimi: PXP failed (%s); using software until restart\n",strerror(errno));
        state.enabled = 0;
    }
    state.software_jobs++;
    state.software_flush(display, area, pixels);
}

int hachimi_pxp_display_attach(lv_display_t * display, const char * framebuffer)
{
    const char * enable = getenv("HACHIMI_PXP");
    if(enable && !strcmp(enable,"0")) { fprintf(stderr,"Hachimi: PXP disabled by HACHIMI_PXP=0\n"); return 0; }
    if(state.engine || !display || display->render_mode != LV_DISPLAY_RENDER_MODE_DIRECT || display->buf_2 ||
       lv_display_get_color_format(display) != LV_COLOR_FORMAT_RGB565) return 0;
    struct fb_fix_screeninfo fixed;
    struct fb_var_screeninfo variable;
    int fd = open(framebuffer,O_RDONLY|O_CLOEXEC);
    if(fd < 0) return 0;
    int valid = ioctl(fd,FBIOGET_FSCREENINFO,&fixed) == 0 && ioctl(fd,FBIOGET_VSCREENINFO,&variable) == 0;
    close(fd);
    if(!valid || variable.bits_per_pixel != 16 || variable.xoffset || variable.yoffset ||
       variable.red.offset != 11 || variable.red.length != 5 || variable.green.offset != 5 ||
       variable.green.length != 6 || variable.blue.offset != 0 || variable.blue.length != 5 ||
       variable.xres != 800 || variable.yres != 480 || fixed.line_length < 1600 ||
       fixed.smem_len < fixed.line_length*variable.yres ||
       !fixed.smem_start || fixed.smem_start > UINT32_MAX - fixed.smem_len) return 0;
    const char * device = getenv("HACHIMI_PXP_DEVICE");
    hachimi_pxp_t * engine = hachimi_pxp_open(device ? device : "/dev/pxp_device",variable.xres,variable.yres);
    if(!engine) {
        fprintf(stderr,"Hachimi: PXP unavailable (%s); using software\n",strerror(errno));
        return 0;
    }
    state.engine = engine;
    state.display = display;
    state.software_flush = display->flush_cb;
    state.original_buffer = lv_display_get_buf_active(display)->data;
    state.width = variable.xres;
    state.height = variable.yres;
    state.stride = fixed.line_length;
    state.destination = fixed.smem_start;
    /* Cached contiguous memory keeps the CPU/NEON drawing path fast. The worker
     * cleans its cache before PXP reads it; PXP writes to the framebuffer. */
    memcpy(hachimi_pxp_pixels(engine),state.original_buffer,state.width*state.height*2);
    lv_display_set_buffers(display,hachimi_pxp_pixels(engine),NULL,state.width*state.height*2,LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display,flush);
    state.enabled = 1;
    fprintf(stderr,"Hachimi: PXP RGB565 flush enabled, cached DMA buffer, threshold 65536 pixels, 100 ms watchdog\n");
    return 1;
}

void hachimi_pxp_display_detach(void)
{
    if(!state.engine) return;
    fprintf(stderr,"Hachimi: PXP flush jobs=%lu, software jobs=%lu\n",state.hardware_jobs,state.software_jobs);
    lv_display_set_flush_cb(state.display,state.software_flush);
    lv_display_set_buffers(state.display,state.original_buffer,NULL,state.width*state.height*2,LV_DISPLAY_RENDER_MODE_DIRECT);
    hachimi_pxp_close(state.engine);
    memset(&state,0,sizeof(state));
}
