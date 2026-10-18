#define _DEFAULT_SOURCE
#include "framework/desktop.h"
#include "framework/ui/ios_status_bar.h"
#include "services/clock.h"
#include "services/filesystem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CHECK(c) do { if(!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static uint8_t buffer[800*40*2];
static uint32_t ticks(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint32_t)((uint64_t)t.tv_sec*1000+t.tv_nsec/1000000); }
static void flush(lv_display_t * display,const lv_area_t * area,uint8_t * pixels)
{ (void)area; (void)pixels; lv_display_flush_ready(display); }
static void pump(unsigned duration)
{ uint32_t start=ticks(); do { lv_timer_handler(); usleep(3000); } while((uint32_t)(ticks()-start)<duration); }
static unsigned timers(void)
{ unsigned n=0; for(lv_timer_t * t=lv_timer_get_next(NULL);t;t=lv_timer_get_next(t)) n++; return n; }
static bool visible_clock(lv_obj_t * object,const char * clock)
{
    if(!lv_obj_is_visible(object)) return false;
    if(lv_obj_check_type(object,&lv_label_class) && !strcmp(lv_label_get_text(object),clock)) return true;
    for(unsigned i=0;i<lv_obj_get_child_count(object);i++)
        if(visible_clock(lv_obj_get_child(object,i),clock)) return true;
    return false;
}
/* System time can cross a minute before the status bar's next 1 s tick.
 * Wait for the observable update instead of comparing unsynchronized samples. */
static void expect_current_clock(lv_obj_t * root)
{
    uint32_t start = ticks();
    do {
        char text[8];
        CHECK(hachimi_clock_format(text, sizeof(text), "%H:%M"));
        if(visible_clock(root, text)) return;
        pump(50);
    } while((uint32_t)(ticks() - start) < 1500);
    CHECK(false);
}

static unsigned started,stopped;
static void start_background(void) { started++; }
static void stop_background(void) { stopped++; }
static const hachimi_app_module_t background={.id="headless",.on_system_start=start_background,.on_system_stop=stop_background};

int main(void)
{
    char home[]="/tmp/hachimi-architecture-XXXXXX";
    CHECK(mkdtemp(home)); CHECK(setenv("HOME",home,1)==0);
    unsetenv("HACHIMI_WEATHER_KEY");
    lv_init(); lv_tick_set_cb(ticks);
    lv_display_t * display=lv_display_create(800,480);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    unsigned baseline=timers();
    /* More subscribers than any current App set; arbitrary deletion order. */
    lv_obj_t * parents[32];
    for(unsigned i=0;i<32;i++) { parents[i]=lv_obj_create(lv_screen_active()); ios_status_bar_create(parents[i],lv_color_white()); }
    char clock[8]; CHECK(hachimi_clock_format(clock,sizeof(clock),"%H:%M"));
    for(unsigned i=0;i<32;i++)
        CHECK(!strcmp(lv_label_get_text(lv_obj_get_child(parents[i],0)),clock));
    for(unsigned i=0;i<32;i+=2) lv_obj_delete(parents[i]);
    pump(1100);
    for(unsigned i=1;i<32;i+=2) lv_obj_delete(parents[i]);
    CHECK(timers()==baseline);
    puts("PASS status bar: 32 subscribers, arbitrary deletion and timer cleanup");
    /* A product with no visible App still starts its explicit background work. */
    const hachimi_app_module_t * modules[]={&background};
    const hachimi_app_catalog_t catalog={.background=modules,.background_count=1};
    CHECK(hachimi_desktop_create(&catalog)); CHECK(started==1);
    CHECK(!hachimi_desktop_create(&catalog));
    CHECK(!hachimi_app_registry_configure(&catalog));
    hachimi_desktop_destroy(); CHECK(stopped==1 && timers()==baseline);
    const hachimi_app_module_t * duplicates[]={&background,&background};
    const hachimi_app_catalog_t invalid={.background=duplicates,.background_count=2};
    CHECK(!hachimi_desktop_create(&invalid));
    puts("PASS product catalog: hidden background module, duplicate rejection and lifecycle");
    ios_desktop_create(); pump(50);
    const hachimi_app_id_t ids[]={HACHIMI_APP_SETTINGS,HACHIMI_APP_FILES,HACHIMI_APP_CALCULATOR,
        HACHIMI_APP_CLOCK,HACHIMI_APP_CALENDAR,HACHIMI_APP_BOOKS,HACHIMI_APP_LED_CONTROL,HACHIMI_APP_MUSIC};
    for(unsigned i=0;i<sizeof(ids)/sizeof(ids[0]);i++) {
        CHECK(hachimi_desktop_launch(ids[i])); pump(550);
        expect_current_clock(lv_screen_active());
        CHECK(hachimi_desktop_home()); pump(400);
    }
    hachimi_desktop_destroy(); pump(50); CHECK(timers()==baseline);
    CHECK(hachimi_fs_remove(home));
    puts("PASS default product: all eight status-bar Apps retain time, shutdown releases timers");
    return 0;
}
