#include "framework/desktop.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);exit(1);} } while(0)
#define W 800
#define H 480
static uint16_t pixels[W*H], reference[12][W*H];
static uint8_t buffer[W*80*2];
static uint32_t tick;
static unsigned backend_begins, backend_ends, backend_draws;
static bool backend_active, expect_content;
static void background_anim(void * var,int32_t value) { *(int32_t *)var=value; }
static bool backend_begin(void * user, const lv_draw_buf_t * background,
                          const lv_draw_buf_t * content,
                          int32_t x, int32_t y, int32_t width, int32_t height, int32_t radius)
{
    (void)user; (void)x; (void)y; (void)width; (void)height; (void)radius;
    CHECK(!backend_active); CHECK(background && background->data);
    CHECK((content != NULL) == expect_content);
    if(content) CHECK(content->header.w == W && content->header.h == H && content->data);
    backend_begins++; backend_active = true; return true;
}
static void backend_draw(void * user, int32_t x, int32_t y, int32_t width, int32_t height,
                         int32_t radius, bool reveal)
{
    (void)user; (void)x; (void)y; (void)reveal;
    CHECK(backend_active); CHECK(width >= 64 && width <= W && height >= 64 && height <= H);
    CHECK(radius >= 0 && radius <= 20); backend_draws++;
}
static void backend_end(void * user)
{
    (void)user; CHECK(backend_active); backend_active = false; backend_ends++;
}
static uint32_t ticks(void){return tick;}
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *data)
{
    unsigned width=area->x2-area->x1+1;
    for(int y=area->y1;y<=area->y2;y++)memcpy(pixels+y*W+area->x1,data+(y-area->y1)*width*2,width*2);
    lv_display_flush_ready(display);
}
static void pump(unsigned ms){for(unsigned i=0;i<ms;i+=5){tick+=5;lv_timer_handler();}}
static void cache_mode(int enabled)
{
#ifdef _WIN32
    CHECK(_putenv_s("HACHIMI_TRANSITION_CACHE",enabled?"1":"0")==0);
#else
    CHECK(setenv("HACHIMI_TRANSITION_CACHE",enabled?"1":"0",1)==0);
#endif
}
static void content_mode(int enabled)
{
#ifdef _WIN32
    CHECK(_putenv_s("HACHIMI_TRANSITION_CONTENT",enabled?"1":"0")==0);
#else
    CHECK(setenv("HACHIMI_TRANSITION_CONTENT",enabled?"1":"0",1)==0);
#endif
}
static void frame(int enabled,unsigned index)
{
    CHECK(index<12);lv_refr_now(NULL);
    if(!enabled)memcpy(reference[index],pixels,sizeof(pixels));
    /* The live clock can cross a minute between the two runs. Compare the
     * complete animated area below the 35px system clock strip exactly. */
    else CHECK(!memcmp(reference[index]+W*35,pixels+W*35,(H-35)*W*2));
}
int main(int argc, char ** argv)
{
    lv_init();lv_tick_set_cb(ticks);
    content_mode(1);
    lv_display_t *display=lv_display_create(W,H);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    const unsigned opening[]={40,100,180,280,380,440},closing[]={40,100,180,260,350};
    for(int enabled=0;enabled<2;enabled++) {
        cache_mode(enabled);ios_desktop_create();pump(50);frame(enabled,0);
        CHECK(hachimi_desktop_launch(HACHIMI_APP_BROWSER));
        unsigned previous=0;
        for(unsigned i=0;i<6;i++){pump(opening[i]-previous);previous=opening[i];frame(enabled,i+1);}
        CHECK(hachimi_desktop_home());previous=0;
        for(unsigned i=0;i<5;i++){pump(closing[i]-previous);previous=closing[i];frame(enabled,i+7);}
        hachimi_desktop_destroy();pump(50);
    }
    puts("PASS 12 desktop/open/close frames: cached and live rendering match pixel-for-pixel");
    for(unsigned i=0;i<3;i++) {
        ios_desktop_create();pump(50);CHECK(hachimi_desktop_launch(HACHIMI_APP_BROWSER));pump(100);
        hachimi_desktop_destroy();pump(50);
    }
    puts("PASS repeated destruction while snapshot animation is active");
    const hachimi_app_id_t content_apps[]={HACHIMI_APP_CALCULATOR,HACHIMI_APP_BOOKS};
    for(unsigned app=0;app<sizeof(content_apps)/sizeof(content_apps[0]);app++) {
    ios_desktop_create();pump(50);
    CHECK(hachimi_desktop_launch(content_apps[app]));pump(400);lv_refr_now(NULL);
    memcpy(reference[0],pixels,sizeof(pixels));
    CHECK(hachimi_desktop_home());lv_refr_now(NULL);
    CHECK(!memcmp(reference[0],pixels,sizeof(pixels)));
    pump(70);lv_refr_now(NULL);
    if(argc > 1 && app==0) {
        FILE * out=fopen(argv[1],"wb");CHECK(out);
        fprintf(out,"P6\n%d %d\n255\n",W,H);
        for(unsigned i=0;i<W*H;i++) {
            unsigned p=pixels[i];
            unsigned char rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};
            CHECK(fwrite(rgb,1,3,out)==3);
        }
        CHECK(!fclose(out));
    }
    pump(250);CHECK(!hachimi_desktop_home());hachimi_desktop_destroy();pump(50);
    }
    puts("PASS Calculator and Books close with the exact app frame, then return to desktop within 320 ms");
    ios_desktop_create(); pump(50);
    int32_t animated=0;
    lv_anim_t concurrent; lv_anim_init(&concurrent);
    lv_anim_set_var(&concurrent,&animated); lv_anim_set_exec_cb(&concurrent,background_anim);
    lv_anim_set_values(&concurrent,0,1000); lv_anim_set_duration(&concurrent,700);
    lv_anim_set_repeat_count(&concurrent,LV_ANIM_REPEAT_INFINITE); lv_anim_start(&concurrent);
    for(unsigned cycle=0;cycle<8;cycle++) {
        CHECK(hachimi_desktop_launch(HACHIMI_APP_BROWSER)); pump(420);
        CHECK(hachimi_desktop_home()); pump(350);
    }
    CHECK(animated>=0 && animated<=1000);
    lv_anim_delete(&animated,background_anim);
    hachimi_desktop_destroy(); pump(50);
    puts("PASS completion refresh never reenters the animation list while other animations run");
    hachimi_desktop_transition_backend_t backend={.begin=backend_begin,.draw=backend_draw,.end=backend_end};
    hachimi_desktop_set_transition_backend(&backend);
    for(unsigned phase=0;phase<3;phase++) {
        content_mode(phase!=2);
        expect_content=false;ios_desktop_create();pump(50);
        CHECK(hachimi_desktop_launch(HACHIMI_APP_BROWSER));
        CHECK(!hachimi_desktop_launch(HACHIMI_APP_BROWSER));
        if(phase) {
            pump(400);expect_content=phase==1;CHECK(hachimi_desktop_home());pump(phase==2?320:50);
        }
        else pump(50);
        hachimi_desktop_destroy();pump(50);CHECK(!backend_active);
    }
    CHECK(backend_begins==5 && backend_ends==5 && backend_draws>0);
    hachimi_desktop_set_transition_backend(NULL);
    puts("PASS direct backend receives close snapshot and ends exactly once on interrupted open/close");
    puts("PASS disabled-content fallback completes and destroys without duplicate backend end");
    return 0;
}
