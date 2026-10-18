#define _DEFAULT_SOURCE
#include "framework/desktop.h"
#include "services/offline_map.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#define CHECK(c) do {if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static uint16_t pixels[800*480],baseline[800*480],repaint[800*480];
static uint8_t draw_buffer[800*80*2];
static lv_indev_data_t touch;
static unsigned rendered;
static uint32_t ticks(void)
{
#ifdef _WIN32
    return GetTickCount();
#else
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint32_t)((uint64_t)t.tv_sec*1000+t.tv_nsec/1000000);
#endif
}
static void sleep_ms(unsigned ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms*1000);
#endif
}
static void pump(unsigned ms){uint32_t start=ticks();do{lv_timer_handler();sleep_ms(3);}while(ticks()-start<ms);}
static void flush(lv_display_t * d,const lv_area_t * a,uint8_t * data)
{
    unsigned w=a->x2-a->x1+1;
    for(int y=a->y1;y<=a->y2;y++)memcpy(pixels+y*800+a->x1,data+(y-a->y1)*w*2,w*2);
    rendered++;lv_display_flush_ready(d);
}
static void read_touch(lv_indev_t * input,lv_indev_data_t * data){(void)input;*data=touch;}
static void tap(int x,int y)
{
    touch.point.x=x;touch.point.y=y;touch.state=LV_INDEV_STATE_PRESSED;pump(60);
    touch.state=LV_INDEV_STATE_RELEASED;pump(200);
}
static lv_obj_t * find_text(lv_obj_t * obj,const char * text)
{
    if(!lv_obj_is_visible(obj))return NULL;
    if(lv_obj_check_type(obj,&lv_label_class)&&!strcmp(lv_label_get_text(obj),text))return obj;
    for(unsigned i=0;i<lv_obj_get_child_count(obj);i++){lv_obj_t * found=find_text(lv_obj_get_child(obj,i),text);if(found)return found;}
    return NULL;
}
static lv_obj_t * find_input(lv_obj_t * obj)
{
    if(!lv_obj_is_visible(obj))return NULL;
    if(lv_obj_check_type(obj,&lv_textarea_class))return obj;
    for(unsigned i=0;i<lv_obj_get_child_count(obj);i++){lv_obj_t * found=find_input(lv_obj_get_child(obj,i));if(found)return found;}
    return NULL;
}
static void capture(const char * prefix,const char * name)
{
    lv_refr_now(NULL);if(!prefix)return;char path[1024];snprintf(path,sizeof(path),"%s-%s.ppm",prefix,name);
    FILE * f=fopen(path,"wb");CHECK(f);fprintf(f,"P6\n800 480\n255\n");
    for(unsigned i=0;i<800*480;i++) {unsigned p=pixels[i];unsigned char rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};CHECK(fwrite(rgb,1,3,f)==3);}
    CHECK(!fclose(f));
}
int main(int argc,char ** argv)
{
    const char * path=getenv("HACHIMI_MAPS_DIR");if(!path)path="assets/maps/beijing";
    hachimi_map_t * service=hachimi_map_open(path);CHECK(service);
    double lat,lon,x,y,lat2,lon2;hachimi_map_center(service,&lat,&lon);
    hachimi_map_world(lat,lon,15,&x,&y);hachimi_map_coords(x,y,15,&lat2,&lon2);
    CHECK(fabs(lat-lat2)<1e-8&&fabs(lon-lon2)<1e-8);CHECK(hachimi_map_distance(lat,lon,lat,lon)<.001);
    unsigned ids[24];CHECK(hachimi_map_search(service,"","公园",lat,lon,ids,24)>0);
    hachimi_map_place_t place=*hachimi_map_place(service,ids[0]);CHECK(!strcmp(place.category,"公园"));
    char alias[256];strcpy(alias,place.alias);char * space=strchr(alias,' ');if(space)*space='\0';
    CHECK(hachimi_map_search(service,alias,"",lat,lon,ids,24)>0);
    CHECK(hachimi_map_search(service,"NO_SUCH_PLACE_8374","",lat,lon,ids,24)==0);
    hachimi_map_begin_frame(service);bool settled=false;const uint8_t * data=NULL;uint32_t start=ticks();
    do{data=hachimi_map_tile(service,15,(int)(x/256),(int)(y/256),NULL,&settled);sleep_ms(3);}while(!settled&&ticks()-start<3000);
    CHECK(data&&settled);hachimi_map_stats_t stats;hachimi_map_stats(service,&stats);CHECK(stats.loads>0&&stats.ready>0);
    CHECK(!hachimi_map_tile(service,15,0,0,NULL,&settled)&&settled);hachimi_map_close(service);
    puts("PASS map metadata, real POI search/pinyin, Mercator coordinates, asynchronous RGB565 tile loading and bounds");
    lv_init();lv_tick_set_cb(ticks);lv_display_t * display=lv_display_create(800,480);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(display,draw_buffer,NULL,sizeof(draw_buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);lv_indev_t * input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,read_touch);
    ios_desktop_create();pump(100);lv_refr_now(NULL);memcpy(baseline,pixels,sizeof(pixels));
    CHECK(hachimi_desktop_launch(HACHIMI_APP_MAPS));pump(1100);
    CHECK(find_text(lv_screen_active(),"北京"));CHECK(!find_text(lv_screen_active(),"离线"));
    capture(argc>1?argv[1]:NULL,"overview");
    lv_obj_t * field=find_input(lv_screen_active());CHECK(field);lv_textarea_set_text(field,alias);pump(100);
    lv_obj_t * item=find_text(lv_screen_active(),place.name);CHECK(item);
    while(!lv_obj_check_type(item,&lv_button_class)){item=lv_obj_get_parent(item);CHECK(item);}
    lv_obj_send_event(item,LV_EVENT_CLICKED,NULL);pump(350);capture(argc>1?argv[1]:NULL,"selected");
    tap(760,100);pump(400);capture(argc>1?argv[1]:NULL,"zoom");
    for(int i=0;i<18;i++) {touch.point.x=660-i*7;touch.point.y=300+i*2;touch.state=LV_INDEV_STATE_PRESSED;pump(17);}
    touch.state=LV_INDEV_STATE_RELEASED;pump(250);capture(argc>1?argv[1]:NULL,"pan");
    memcpy(repaint,pixels,sizeof(pixels));lv_obj_invalidate(lv_screen_active());lv_refr_now(NULL);
    CHECK(!memcmp(repaint,pixels,sizeof(pixels)));
    puts("PASS partial map repaint is pixel-identical to a full redraw including panel corners and shadows");
    tap(32,432);pump(420);lv_refr_now(NULL);CHECK(!hachimi_desktop_home());
    CHECK(!memcmp(baseline+800*35,pixels+800*35,(480-35)*800*2));
    hachimi_desktop_destroy();pump(50);
    for(unsigned i=0;i<4;i++){ios_desktop_create();pump(20);CHECK(hachimi_desktop_launch(HACHIMI_APP_MAPS));pump(360);hachimi_desktop_destroy();pump(30);}
    CHECK(rendered>20);puts("PASS CarPlay map launch/search/select/zoom/drag/return and destruction while tiles are loading");return 0;
}
