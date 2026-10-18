#include "framework/desktop.h"
#include "services/weather.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <time.h>
#endif
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static uint16_t pixels[800*480];
static uint8_t buffer[800*80*2];
static uint32_t ticks(void){
#ifdef _WIN32
    return GetTickCount();
#else
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint32_t)((uint64_t)t.tv_sec*1000+t.tv_nsec/1000000);
#endif
}
static void flush(lv_display_t * d,const lv_area_t * a,uint8_t * b)
{
    int w=a->x2-a->x1+1;for(int y=a->y1;y<=a->y2;y++)memcpy(pixels+y*800+a->x1,b+(y-a->y1)*w*2,w*2);lv_display_flush_ready(d);
}
static void pump(unsigned ms)
{
    uint32_t start=ticks();do{lv_timer_handler();
#ifdef _WIN32
        Sleep(3);
#else
        usleep(3000);
#endif
    }while(ticks()-start<ms);
}
static lv_obj_t * find(lv_obj_t * o,const char * text)
{
    if(!lv_obj_is_visible(o))return NULL;
    if(lv_obj_check_type(o,&lv_label_class)&&!strcmp(lv_label_get_text(o),text))return o;
    for(unsigned k=0;k<lv_obj_get_child_count(o);k++){lv_obj_t * f=find(lv_obj_get_child(o,k),text);if(f)return f;}return NULL;
}
static void click(const char * text)
{
    lv_obj_t * o=find(lv_screen_active(),text);CHECK(o);while(o&&!lv_obj_has_flag(o,LV_OBJ_FLAG_CLICKABLE))o=lv_obj_get_parent(o);CHECK(o);lv_obj_send_event(o,LV_EVENT_CLICKED,NULL);pump(80);
}
static lv_obj_t * keyboard_in(lv_obj_t * o)
{
    if(lv_obj_check_type(o,&lv_keyboard_class))return o;
    for(unsigned k=0;k<lv_obj_get_child_count(o);k++){lv_obj_t * found=keyboard_in(lv_obj_get_child(o,k));if(found)return found;}
    return NULL;
}
static unsigned timers(void){unsigned n=0;lv_timer_t * t=NULL;while((t=lv_timer_get_next(t)))n++;return n;}
static void capture(const char * name)
{
    FILE * f=fopen(name,"wb");CHECK(f);fprintf(f,"P6\n800 480\n255\n");
    for(unsigned k=0;k<800*480;k++){uint16_t p=pixels[k];unsigned char b[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};CHECK(fwrite(b,1,3,f)==3);}fclose(f);
}
static void parser_tests(void)
{
    hachimi_weather_t d={0};
    CHECK(hachimi_weather_decode(&d,0,"{\"results\":[{\"location\":{\"name\":\"\\u5317\\u4eac\"},\"now\":{\"text\":\"晴\",\"code\":\"0\",\"temperature\":\"-3\"},\"last_update\":\"2026-10-17T10:00:00+08:00\"}]}"));
    CHECK(!strcmp(d.city,"北京")&&d.temperature==-3&&d.now_valid);
    CHECK(!hachimi_weather_decode(&d,0,"{\"results\":[{\"location\":{\"name\":\"上海\"},\"now\":{\"text\":\"雨\",\"code\":\"13\"}}]}"));
    CHECK(!strcmp(d.city,"北京")&&d.temperature==-3);
    CHECK(!hachimi_weather_decode(&d,1,"{\"results\":[{\"daily\":[{\"date\":\"2026-10-17\",\"text_day\":\"晴\",\"code_day\":\"0\",\"low\":\"30\",\"high\":\"2\"}]}]}"));
    CHECK(!d.days);
    CHECK(hachimi_weather_decode(&d,2,"{\"results\":[{\"suggestion\":{\"dressing\":{\"brief\":\"舒适\"},\"uv\":{\"brief\":null}}}]}"));
    CHECK(!strcmp(d.brief[0],"舒适")&&!d.brief[1][0]);
    CHECK(hachimi_weather_decode(&d,2,"{\"results\":[{\"suggestion\":[{\"dressing\":{\"brief\":\"热\",\"details\":\"穿轻薄衣服\"}}]}]}"));
    CHECK(!strcmp(d.brief[0],"热")&&!strcmp(d.details[0],"穿轻薄衣服"));
    CHECK(hachimi_weather_decode(&d,2,"{\"results\":[{\"suggestion\":{\"travel\":{\"brief\":\"适宜\"}}}]}"));
    CHECK(!strcmp(d.life_names[3],"旅游")&&!strcmp(d.brief[3],"适宜")&&!d.brief[0][0]);
    CHECK(!hachimi_weather_decode(&d,0,"{\"status_code\":\"AP010003\"}"));CHECK(strstr(d.errors[0],"AP010003"));
    CHECK(!hachimi_weather_decode(&d,0,"{\"results\":["));
    CHECK(!hachimi_weather_decode(&d,0,"{\"results\":[] } garbage"));
    CHECK(!hachimi_weather_decode(&d,0,"{\"results\":[],\"a\":\"\\u12\"}"));
    puts("PASS JSON: Unicode, negative temperatures, missing data, errors, both suggestion formats, malformed payloads");
}
int main(int argc,char ** argv)
{
    parser_tests();bool live=argc>1&&!strcmp(argv[1],"--live");
    lv_init();lv_tick_set_cb(ticks);lv_display_t * d=lv_display_create(800,480);
    lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(d,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
    unsigned baseline=timers();ios_desktop_create();pump(100);
    CHECK(hachimi_desktop_launch(HACHIMI_APP_WEATHER));pump(1000);
    CHECK(find(lv_screen_active(),"三日预报"));CHECK(find(lv_screen_active(),"生活建议"));
    CHECK(find(lv_screen_active(),"切换城市"));
    if(live){if(!hachimi_weather_busy())CHECK(hachimi_weather_refresh());
        uint32_t start=ticks();while(hachimi_weather_busy()&&ticks()-start<65000)pump(50);
        const hachimi_weather_t * weather=hachimi_weather_data();CHECK(!hachimi_weather_busy());
        for(unsigned k=0;k<3;k++)if(weather->errors[k][0])fprintf(stderr,"endpoint %u: %s\n",k,weather->errors[k]);
        CHECK(weather->now_valid&&weather->days==3&&weather->brief[0][0]);CHECK(!weather->errors[0][0]&&!weather->errors[1][0]&&!weather->errors[2][0]);
        CHECK(find(lv_screen_active(),weather->city));
        puts("PASS live HTTPS: current conditions, 3 forecast days and lifestyle suggestions");
    }
    pump(100);capture(argc>2?argv[2]:"weather.ppm");
    click("切换城市");CHECK(find(lv_screen_active(),"选择城市"));CHECK(find(lv_screen_active(),"成都"));
    lv_obj_t * keyboard=keyboard_in(lv_screen_active());CHECK(keyboard);lv_obj_update_layout(lv_screen_active());
    lv_area_t bounds;lv_obj_get_coords(keyboard,&bounds);CHECK(bounds.y1>=195&&bounds.y2<=453&&bounds.x1>=16&&bounds.x2<=783);
    click("完成");
    if(hachimi_weather_data()->brief[0][0]){click("穿衣");CHECK(find(lv_screen_active(),"当日生活建议"));click("关闭");}
    for(int k=0;k<3;k++){hachimi_desktop_home();pump(400);CHECK(hachimi_desktop_launch(HACHIMI_APP_WEATHER));pump(500);}
    if(!live){click("切换城市");click("上海");CHECK(!hachimi_weather_data()->now_valid&&!hachimi_weather_data()->days);}
    hachimi_desktop_destroy();pump(40);CHECK(timers()==baseline);
    ios_desktop_create();CHECK(hachimi_desktop_launch(HACHIMI_APP_WEATHER));pump(500);hachimi_desktop_destroy();pump(40);CHECK(timers()==baseline);
    puts("PASS weather UI: registration, city picker, lifestyle detail, return/reopen, destroy/recreate without timer leaks");return 0;
}
