#define _POSIX_C_SOURCE 200809L
#include "framework/desktop.h"
#include "services/calendar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(v) do {if(!(v)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#v);exit(1);}}while(0)
static uint16_t pixels[800*480];
static uint8_t draw_buffer[800*80*2];
static uint32_t tick;
static const char * artifacts;
static void capture(const char * name);
static uint32_t ticks(void){return tick;}
static void flush(lv_display_t * display,const lv_area_t * area,uint8_t * data)
{
    int width=area->x2-area->x1+1;
    for(int y=area->y1;y<=area->y2;y++)memcpy(pixels+y*800+area->x1,data+(y-area->y1)*width*2,width*2);
    lv_display_flush_ready(display);
}
static void pump(unsigned ms){for(unsigned i=0;i<ms;i+=5){tick+=5;lv_timer_handler();}}
static lv_obj_t * text(lv_obj_t * parent,const char * value)
{
    if(!lv_obj_is_visible(parent))return NULL;
    if(lv_obj_check_type(parent,&lv_label_class)&&!strcmp(lv_label_get_text(parent),value))return parent;
    for(uint32_t i=0;i<lv_obj_get_child_count(parent);i++){lv_obj_t * found=text(lv_obj_get_child(parent,(int)i),value);if(found)return found;}
    return NULL;
}
static lv_obj_t * button_text(lv_obj_t * parent,const char * value)
{
    if(!lv_obj_is_visible(parent))return NULL;
    if(lv_obj_check_type(parent,&lv_label_class)&&!strcmp(lv_label_get_text(parent),value)&&
       lv_obj_check_type(lv_obj_get_parent(parent),&lv_button_class))return parent;
    for(uint32_t i=0;i<lv_obj_get_child_count(parent);i++){lv_obj_t * found=button_text(lv_obj_get_child(parent,(int)i),value);if(found)return found;}
    return NULL;
}
static void click(const char * value)
{
    lv_obj_update_layout(lv_screen_active());lv_obj_t * target=button_text(lv_screen_active(),value);
    if(!target)fprintf(stderr,"Missing visible text: %s\n",value);
    CHECK(target);
    while(target&&!lv_obj_has_flag(target,LV_OBJ_FLAG_CLICKABLE))target=lv_obj_get_parent(target);
    CHECK(target);lv_obj_send_event(target,LV_EVENT_CLICKED,NULL);pump(60);
}
static lv_obj_t * find_widget(lv_obj_t * parent,const lv_obj_class_t * type,unsigned * index)
{
    if(!lv_obj_is_visible(parent))return NULL;
    if(lv_obj_check_type(parent,type)&&(*index)--==0)return parent;
    for(uint32_t i=0;i<lv_obj_get_child_count(parent);i++){lv_obj_t * found=find_widget(lv_obj_get_child(parent,(int)i),type,index);if(found)return found;}
    return NULL;
}
static lv_obj_t * widget(const lv_obj_class_t * type,unsigned index)
{
    unsigned requested=index;
    lv_obj_t * parent=lv_screen_active();
    lv_obj_t * heading=text(parent,"新建日程");
    if(!heading)heading=text(parent,"编辑日程");
    if(heading&&type!=&lv_keyboard_class)parent=lv_obj_get_parent(heading);
    lv_obj_t * found=find_widget(parent,type,&index);
    if(!found){fprintf(stderr,"Missing widget %s #%u\n",type==&lv_textarea_class?"textarea":type==&lv_keyboard_class?"keyboard":type==&lv_dropdown_class?"dropdown":"switch",requested);capture("calendar-failure");}
    CHECK(found);return found;
}
static unsigned timer_count(void)
{
    unsigned count=0;for(lv_timer_t * t=lv_timer_get_next(NULL);t;t=lv_timer_get_next(t))count++;return count;
}
static void check_app_edge_gestures(bool enabled)
{
    static const lv_coord_t positions[][4]={{0,60,36,384},{764,60,36,384},{36,444,728,36}};
    lv_obj_t * screen=lv_screen_active();uint32_t count=lv_obj_get_child_count(screen);
    for(unsigned zone=0;zone<3;zone++) {
        bool found=false;
        for(uint32_t i=0;i<count;i++) {
            lv_obj_t * child=lv_obj_get_child(screen,(int)i);
            if(lv_obj_get_x(child)!=positions[zone][0]||lv_obj_get_y(child)!=positions[zone][1]||
               lv_obj_get_width(child)!=positions[zone][2]||lv_obj_get_height(child)!=positions[zone][3]||
               !lv_obj_has_flag(child,LV_OBJ_FLAG_CLICKABLE))continue;
            found=true;CHECK(lv_obj_has_flag(child,LV_OBJ_FLAG_HIDDEN)==!enabled);break;
        }
        CHECK(found);
    }
}
static void capture(const char * name)
{
    if(!artifacts)return;
    lv_refr_now(NULL);char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",artifacts,name);
    FILE * file=fopen(path,"wb");CHECK(file);fprintf(file,"P6\n800 480\n255\n");
    for(unsigned i=0;i<800*480;i++){uint16_t p=pixels[i];uint8_t rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};CHECK(fwrite(rgb,1,3,file)==3);}
    fclose(file);
}
static void model_checks(void)
{
    CHECK(hachimi_date_month_days(2000,2)==29);CHECK(hachimi_date_month_days(1900,2)==28);
    CHECK(hachimi_date_month_days(2024,2)==29);CHECK(hachimi_date_month_days(2026,2)==28);
    CHECK(!hachimi_date_valid((hachimi_date_t){2026,2,29}));
    CHECK(hachimi_date_weekday((hachimi_date_t){2026,10,15})==4);
    CHECK(hachimi_date_compare(hachimi_date_add_days((hachimi_date_t){2024,2,28},1),(hachimi_date_t){2024,2,29})==0);
    CHECK(hachimi_date_compare(hachimi_date_add_days((hachimi_date_t){2026,1,1},-1),(hachimi_date_t){2025,12,31})==0);
    hachimi_calendar_t calendar;CHECK(hachimi_calendar_load(&calendar));CHECK(calendar.count==0);
    hachimi_calendar_event_t item={0};item.date=(hachimi_date_t){2026,10,15};item.start_minute=540;item.end_minute=600;
    snprintf(item.title,sizeof(item.title),"项目讨论 | 日历");snprintf(item.location,sizeof(item.location),"A\nB|C");
    CHECK(hachimi_calendar_put(&calendar,&item));CHECK(item.id!=0);
    hachimi_calendar_t reloaded;CHECK(hachimi_calendar_load(&reloaded));CHECK(reloaded.count==1);
    CHECK(!strcmp(reloaded.events[0].title,item.title));CHECK(!strcmp(reloaded.events[0].location,item.location));
    item.end_minute=item.start_minute;CHECK(!hachimi_calendar_put(&calendar,&item));CHECK(calendar.events[0].end_minute==600);
    item.end_minute=660;CHECK(hachimi_calendar_put(&calendar,&item));
    char original[1024];snprintf(original,sizeof(original),"%s",calendar.path);calendar.path[0]='\0';
    CHECK(!hachimi_calendar_delete(&calendar,item.id));CHECK(calendar.count==1);
    snprintf(calendar.path,sizeof(calendar.path),"%s",original);CHECK(hachimi_calendar_delete(&calendar,item.id));
    CHECK(hachimi_calendar_load(&reloaded));CHECK(reloaded.count==0);
    puts("PASS Gregorian dates, leap years, boundaries, UTF-8 persistence and failure rollback");
}
int main(int argc,char ** argv)
{
    CHECK(argc>=2);CHECK(hachimi_fs_is_directory(argv[1]));
    bool keyboard_dismiss_only=argc>2&&!strcmp(argv[2],"--keyboard-dismiss");
#ifdef _WIN32
    CHECK(_putenv_s("USERPROFILE",argv[1])==0);
#else
    CHECK(setenv("HOME",argv[1],1)==0);
#endif
    if(argc>2&&!keyboard_dismiss_only){artifacts=argv[2];CHECK(hachimi_fs_is_directory(artifacts));}
    model_checks();lv_init();lv_tick_set_cb(ticks);
    lv_display_t * display=lv_display_create(800,480);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,draw_buffer,NULL,sizeof(draw_buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    ios_desktop_create();pump(50);
    CHECK(hachimi_desktop_launch(HACHIMI_APP_CALENDAR));pump(650);check_app_edge_gestures(true);capture("calendar-month-empty");
    if(keyboard_dismiss_only) {
        click("+");lv_obj_send_event(widget(&lv_textarea_class,0),LV_EVENT_CLICKED,NULL);pump(50);
        check_app_edge_gestures(false);lv_obj_t * keyboard=widget(&lv_keyboard_class,0);
        lv_obj_send_event(keyboard,LV_EVENT_CANCEL,NULL);pump(50);
        CHECK(lv_obj_has_flag(keyboard,LV_OBJ_FLAG_HIDDEN));CHECK(text(lv_screen_active(),"新建日程"));
        check_app_edge_gestures(true);puts("PASS calendar stays open when dismissing the soft keyboard");
        hachimi_desktop_destroy();return 0;
    }
    CHECK(pixels[2*800+5]==pixels[42*800+5]);CHECK(pixels[2*800+5]!=pixels[2*800+210]);
    click("+");capture("calendar-new-event");click("存储");CHECK(text(lv_screen_active(),"请输入标题"));
    lv_obj_send_event(widget(&lv_textarea_class,0),LV_EVENT_CLICKED,NULL);pump(50);capture("calendar-keyboard");
    check_app_edge_gestures(false);lv_obj_t * keyboard=widget(&lv_keyboard_class,0);
    lv_obj_send_event(keyboard,LV_EVENT_CANCEL,NULL);pump(50);
    CHECK(lv_obj_has_flag(keyboard,LV_OBJ_FLAG_HIDDEN));CHECK(text(lv_screen_active(),"新建日程"));check_app_edge_gestures(true);
    lv_textarea_set_text(widget(&lv_textarea_class,0),"项目讨论");lv_textarea_set_text(widget(&lv_textarea_class,1),"会议室 A");
    click("存储");CHECK(text(lv_screen_active(),"新建日程")==NULL);
    click("列表");CHECK(text(lv_screen_active(),"项目讨论"));capture("calendar-list");
    click("项目讨论");lv_textarea_set_text(widget(&lv_textarea_class,0),"项目评审");
    lv_dropdown_set_selected(widget(&lv_dropdown_class,7),1); /* calendar category */
    capture("calendar-edit-event");click("存储");CHECK(text(lv_screen_active(),"项目评审"));
    click("+");lv_textarea_set_text(widget(&lv_textarea_class,0),"并行会议");
    lv_dropdown_set_selected(widget(&lv_dropdown_class,4),30);lv_dropdown_set_selected(widget(&lv_dropdown_class,6),30);
    lv_dropdown_set_selected(widget(&lv_dropdown_class,7),1);click("存储");
    click("工作");CHECK(!text(lv_screen_active(),"项目评审"));click("工作");CHECK(text(lv_screen_active(),"项目评审"));
    click("周");capture("calendar-week");click("日");capture("calendar-day");
    click("月");capture("calendar-month");click("年");CHECK(text(lv_screen_active(),"3月"));capture("calendar-year");
    click("搜索");lv_obj_t * input=widget(&lv_textarea_class,0);lv_textarea_set_text(input,"不存在的日程");
    pump(50);CHECK(text(lv_screen_active(),"无搜索结果"));lv_textarea_set_text(input,"项目评审");pump(50);
    CHECK(text(lv_screen_active(),"项目评审"));
    lv_obj_t * key=widget(&lv_keyboard_class,0);lv_obj_send_event(key,LV_EVENT_READY,NULL);pump(50);capture("calendar-search");
    click("项目评审");click("删除日程");
    lv_obj_t * confirmation=lv_obj_get_parent(text(lv_screen_active(),"删除这个日程？"));
    lv_obj_t * cancel=lv_obj_get_parent(text(confirmation,"取消"));lv_obj_send_event(cancel,LV_EVENT_CLICKED,NULL);pump(50);
    CHECK(text(lv_screen_active(),"编辑日程"));
    click("删除日程");click("删除");CHECK(!button_text(lv_screen_active(),"项目评审"));
    puts("PASS add/edit/delete, input validation, category filtering, all views and search");
    click("+");lv_textarea_set_text(widget(&lv_textarea_class,0),"全天安排");
    lv_obj_t * toggle=widget(&lv_switch_class,0);lv_obj_add_state(toggle,LV_STATE_CHECKED);lv_obj_send_event(toggle,LV_EVENT_VALUE_CHANGED,NULL);
    click("存储");click("月");
    CHECK(hachimi_desktop_home());pump(450);CHECK(hachimi_desktop_launch(HACHIMI_APP_CALCULATOR));pump(650);
    CHECK(hachimi_desktop_home());pump(450);CHECK(hachimi_desktop_launch(HACHIMI_APP_CALENDAR));pump(650);
    click("列表");CHECK(text(lv_screen_active(),"全天安排"));
    hachimi_desktop_destroy();pump(50);unsigned baseline=timer_count();
    ios_desktop_create();CHECK(hachimi_desktop_launch(HACHIMI_APP_CALENDAR));pump(650);click("列表");CHECK(text(lv_screen_active(),"全天安排"));
    hachimi_desktop_destroy();pump(50);CHECK(timer_count()==baseline);CHECK(lv_obj_get_child_count(lv_layer_top())==0);
    puts("PASS all-day event, app switching, restart persistence, full-height sidebar and timer cleanup");
    /* Keep the disposable fixture reusable for the next run. */
    hachimi_calendar_t calendar;CHECK(hachimi_calendar_load(&calendar));
    while(calendar.count)CHECK(hachimi_calendar_delete(&calendar,calendar.events[0].id));
    return 0;
}
