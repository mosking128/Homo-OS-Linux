#include "ios_calendar.h"
#include "framework/desktop.h"
#include "framework/ui/fonts.h"
#include "framework/ui/ios_status_bar.h"
#include "services/calendar.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define RED 0xff3b30
#define INK 0x1c1c1e
#define MUTED 0x8e8e93
#define SIDE 0xf2f2f7
#define LINE 0xe5e5ea
typedef enum { VIEW_DAY, VIEW_WEEK, VIEW_MONTH, VIEW_YEAR, VIEW_LIST, VIEW_SEARCH } calendar_view_t;
static const char * const weekdays[] = {"日","一","二","三","四","五","六"};
static const char * const categories[] = {"个人","工作","家庭"};
static const uint32_t colors[] = {0x0a84ff, 0xff9500, 0x34c759};
static const uint32_t tints[] = {0xe8f2ff, 0xfff1dc, 0xe7f6ec};
static hachimi_calendar_t store;
static hachimi_date_t selected, today;
static calendar_view_t current_view = VIEW_MONTH;
static bool visible_categories[3] = {true,true,true};
static lv_obj_t * root;
static lv_obj_t * content;
static lv_obj_t * sidebar;
static lv_obj_t * results;
static lv_obj_t * search_input;
static lv_obj_t * keyboard;
static lv_obj_t * editor;
static lv_obj_t * editor_overlay;
static lv_obj_t * title_input;
static lv_obj_t * location_input;
static lv_obj_t * date_fields[3];
static lv_obj_t * time_fields[4];
static lv_obj_t * all_day_switch;
static lv_obj_t * category_dropdown;
static lv_obj_t * editor_error;
static lv_timer_t * refresh_timer;
static uint32_t editing_id;
static char search_query[97];
static hachimi_date_t grid_dates[42];

static void render(void);
static void show_editor(uint32_t id);
static void render_list(void);

static hachimi_date_t local_date(void)
{
    time_t now = time(NULL); struct tm * value = localtime(&now);
    hachimi_date_t date = {2026,1,1};
    if(value) { date.year = value->tm_year + 1900; date.month = value->tm_mon + 1; date.day = value->tm_mday; }
    return date;
}
static lv_obj_t * panel(lv_obj_t * parent, int x, int y, int w, int h, uint32_t bg, int radius)
{
    lv_obj_t * obj = lv_obj_create(parent); lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj,x,y); lv_obj_set_size(obj,w,h);
    lv_obj_set_style_bg_color(obj,lv_color_hex(bg),0); lv_obj_set_style_bg_opa(obj,255,0);
    lv_obj_set_style_radius(obj,radius,0); lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}
static lv_obj_t * label(lv_obj_t * parent, const char * text, int x,int y,int w,int h,unsigned size,uint32_t color)
{
    lv_obj_t * obj = lv_label_create(parent); lv_label_set_text(obj,text);
    lv_obj_set_pos(obj,x,y); lv_obj_set_size(obj,w,h);
    lv_obj_set_style_text_font(obj,hachimi_ui_font(size),0);
    lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
    lv_label_set_long_mode(obj,LV_LABEL_LONG_MODE_DOTS);
    lv_obj_clear_flag(obj,LV_OBJ_FLAG_CLICKABLE);
    return obj;
}
static lv_obj_t * button(lv_obj_t * parent,const char * text,int x,int y,int w,int h,
                         uint32_t bg,uint32_t fg,unsigned size,lv_event_cb_t cb,uintptr_t data)
{
    lv_obj_t * obj = lv_button_create(parent); lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj,x,y); lv_obj_set_size(obj,w,h);
    lv_obj_set_style_bg_color(obj,lv_color_hex(bg),0); lv_obj_set_style_bg_opa(obj,255,0);
    lv_obj_set_style_radius(obj,8,0); lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t * text_obj = label(obj,text,0,0,w,h,size,fg);
    lv_obj_set_size(text_obj,LV_SIZE_CONTENT,LV_SIZE_CONTENT); lv_obj_center(text_obj);
    if(cb) lv_obj_add_event_cb(obj,cb,LV_EVENT_CLICKED,(void *)data);
    return obj;
}
static void rule(lv_obj_t * parent,int x,int y,int w,int h)
{
    lv_obj_t * obj = panel(parent,x,y,w,h,LINE,0); lv_obj_clear_flag(obj,LV_OBJ_FLAG_CLICKABLE);
}
static bool same(hachimi_date_t a,hachimi_date_t b) { return hachimi_date_compare(a,b) == 0; }
static bool included(const hachimi_calendar_event_t * event)
{
    return event->category < 3 && visible_categories[event->category];
}
static void format_time(const hachimi_calendar_event_t * event,char * text,size_t capacity)
{
    if(event->all_day) snprintf(text,capacity,"全天");
    else snprintf(text,capacity,"%02u:%02u–%02u:%02u",event->start_minute/60,event->start_minute%60,
                  event->end_minute/60,event->end_minute%60);
}
static void event_cb(lv_event_t * event)
{
    show_editor((uint32_t)(uintptr_t)lv_event_get_user_data(event));
}
static void day_cb(lv_event_t * event)
{
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if(index >= 42) return;
    bool open = same(selected,grid_dates[index]); selected = grid_dates[index];
    if(open) current_view = VIEW_DAY;
    render();
}
static void mini_day_cb(lv_event_t * event)
{
    unsigned day = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    selected.day = (int)day; render();
}
static void view_cb(lv_event_t * event)
{
    current_view = (calendar_view_t)(uintptr_t)lv_event_get_user_data(event); render();
}
static void move_month(int delta)
{
    int value = selected.year * 12 + selected.month - 1 + delta;
    if(value < 1900 * 12 || value > 2199 * 12 + 11) return;
    selected.year = value / 12; selected.month = value % 12 + 1;
    int maximum = hachimi_date_month_days(selected.year,selected.month);
    if(selected.day > maximum) selected.day = maximum;
}
static void navigate_cb(lv_event_t * event)
{
    int action = (int)(intptr_t)lv_event_get_user_data(event);
    if(action == 0) selected = today;
    else if(current_view == VIEW_YEAR) move_month(action * 12);
    else if(current_view == VIEW_DAY) selected = hachimi_date_add_days(selected,action);
    else if(current_view == VIEW_WEEK) selected = hachimi_date_add_days(selected,action * 7);
    else move_month(action);
    render();
}
static void category_cb(lv_event_t * event)
{
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if(index < 3) visible_categories[index] = !visible_categories[index];
    else for(unsigned i=0;i<3;i++) visible_categories[i] = true;
    render();
}
static void render_sidebar(void)
{
    lv_obj_clean(sidebar);
    label(sidebar,"日历",22,58,145,38,28,INK);
    char text[40]; snprintf(text,sizeof(text),"%d年 %d月",selected.year,selected.month);
    label(sidebar,text,22,110,150,25,16,RED);
    for(int i=0;i<7;i++) {
        lv_obj_t * obj = label(sidebar,weekdays[i],17+i*24,144,24,20,12,MUTED);
        lv_obj_set_style_text_align(obj,LV_TEXT_ALIGN_CENTER,0);
    }
    hachimi_date_t first = {selected.year,selected.month,1};
    int offset = hachimi_date_weekday(first);
    for(int day=1;day<=hachimi_date_month_days(selected.year,selected.month);day++) {
        int cell=offset+day-1;
        snprintf(text,sizeof(text),"%d",day);
        bool chosen=day==selected.day;
        lv_obj_t * obj=button(sidebar,text,17+(cell%7)*24,166+(cell/7)*24,23,23,
                              chosen?RED:SIDE,chosen?0xffffff:INK,12,mini_day_cb,(uintptr_t)day);
        lv_obj_set_style_radius(obj,LV_RADIUS_CIRCLE,0);
    }
    rule(sidebar,20,315,154,1);
    label(sidebar,"我的日历",22,330,150,20,14,MUTED);
    for(unsigned i=0;i<3;i++) {
        lv_obj_t * row=button(sidebar,categories[i],20,356+(int)i*29,154,27,SIDE,INK,14,category_cb,i);
        lv_obj_t * dot=panel(row,8,6,15,15,visible_categories[i]?colors[i]:SIDE,8);
        lv_obj_set_style_border_width(dot,1,0); lv_obj_set_style_border_color(dot,lv_color_hex(colors[i]),0);
        lv_obj_clear_flag(dot,LV_OBJ_FLAG_CLICKABLE);
        if(visible_categories[i]) {
            lv_obj_t * tick=label(dot,LV_SYMBOL_OK,0,-1,15,17,12,0xffffff);
            lv_obj_set_style_text_font(tick,&lv_font_montserrat_14,0);lv_obj_set_style_text_align(tick,LV_TEXT_ALIGN_CENTER,0);
        }
    }
}
static void render_month(void)
{
    hachimi_date_t first={selected.year,selected.month,1};
    int offset=hachimi_date_weekday(first);
    for(int col=0;col<7;col++) {
        lv_obj_t * obj=label(content,weekdays[col],16+col*80,97,80,22,14,(col==0||col==6)?MUTED:INK);
        lv_obj_set_style_text_align(obj,LV_TEXT_ALIGN_CENTER,0);
    }
    for(unsigned i=0;i<42;i++) {
        grid_dates[i]=hachimi_date_add_days(first,(int)i-offset);
        int x=16+(int)(i%7)*80,y=123+(int)(i/7)*43;
        lv_obj_t * cell=panel(content,x,y,80,43,(i%7==0||i%7==6)?0xfafafa:0xffffff,0);
        lv_obj_add_event_cb(cell,day_cb,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
        rule(cell,0,0,80,1);
        char text[16];snprintf(text,sizeof(text),"%d",grid_dates[i].day);
        bool is_today=same(grid_dates[i],today), chosen=same(grid_dates[i],selected);
        lv_obj_t * badge=panel(cell,27,3,25,25,is_today?RED:(chosen?0xe8e8ed:((i%7==0||i%7==6)?0xfafafa:0xffffff)),13);
        lv_obj_clear_flag(badge,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t * num=label(badge,text,0,1,25,23,16,is_today?0xffffff:(grid_dates[i].month==selected.month?INK:0xc7c7cc));
        lv_obj_set_style_text_align(num,LV_TEXT_ALIGN_CENTER,0);
        unsigned count=0;
        for(unsigned j=0;j<store.count;j++) {
            const hachimi_calendar_event_t * event=&store.events[j];
            if(!included(event)||!same(event->date,grid_dates[i]))continue;
            if(count++==0) {
                lv_obj_t * tag=button(cell,event->title,3,28,74,14,tints[event->category],colors[event->category],12,event_cb,event->id);
                lv_obj_t * caption=lv_obj_get_child(tag,0);lv_obj_set_size(caption,70,16);lv_obj_set_pos(caption,2,0);
            }
        }
        if(count>1) {
            snprintf(text,sizeof(text),"+%u",count-1);
            label(cell,text,56,6,23,18,12,MUTED);
        }
    }
}
static void year_month_cb(lv_event_t * event)
{
    selected.month=(int)(uintptr_t)lv_event_get_user_data(event);
    int maximum=hachimi_date_month_days(selected.year,selected.month);if(selected.day>maximum)selected.day=maximum;
    current_view=VIEW_MONTH;render();
}
static void render_year(void)
{
    lv_obj_t * scroll=panel(content,12,98,570,290,0xffffff,0);
    lv_obj_add_flag(scroll,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(scroll,LV_DIR_VER);
    for(int month=1;month<=12;month++) {
        int x=((month-1)%3)*188,y=((month-1)/3)*148;
        char text[16];snprintf(text,sizeof(text),"%d月",month);
        lv_obj_t * card=panel(scroll,x,y,180,142,0xffffff,0);
        lv_obj_add_event_cb(card,year_month_cb,LV_EVENT_CLICKED,(void *)(uintptr_t)month);
        label(card,text,6,1,170,23,18,RED);
        for(int c=0;c<7;c++)label(card,weekdays[c],6+c*24,28,23,18,12,MUTED);
        hachimi_date_t first={selected.year,month,1};int offset=hachimi_date_weekday(first);
        for(int day=1;day<=hachimi_date_month_days(selected.year,month);day++) {
            int cell=offset+day-1;snprintf(text,sizeof(text),"%d",day);
            hachimi_date_t date={selected.year,month,day};
            lv_obj_t * num=label(card,text,6+(cell%7)*24,47+(cell/7)*15,23,18,12,same(date,today)?RED:INK);
            lv_obj_set_style_text_align(num,LV_TEXT_ALIGN_CENTER,0);
        }
    }
}
static void timeline_day_cb(lv_event_t * event)
{
    unsigned offset=(unsigned)(uintptr_t)lv_event_get_user_data(event);
    hachimi_date_t start=hachimi_date_add_days(selected,-hachimi_date_weekday(selected));
    selected=hachimi_date_add_days(start,(int)offset);current_view=VIEW_DAY;render();
}
static void render_timeline(void)
{
    bool week=current_view==VIEW_WEEK; int columns=week?7:1,col_width=week?73:511;
    hachimi_date_t start=week?hachimi_date_add_days(selected,-hachimi_date_weekday(selected)):selected;
    for(int col=0;col<columns;col++) {
        hachimi_date_t date=hachimi_date_add_days(start,col);char text[32];
        snprintf(text,sizeof(text),"周%s %d",weekdays[hachimi_date_weekday(date)],date.day);
        button(content,text,65+col*col_width,92,col_width,34,0xffffff,same(date,today)?RED:INK,week?14:18,
               week?timeline_day_cb:NULL,(uintptr_t)col);
    }
    label(content,"全天",16,133,44,22,12,MUTED);
    for(int col=0;col<columns;col++) {
        lv_obj_t * lane=panel(content,65+col*col_width,130,col_width-3,44,0xffffff,0);
        lv_obj_add_flag(lane,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(lane,LV_DIR_VER);
        unsigned row=0;
        for(unsigned i=0;i<store.count;i++) {
            const hachimi_calendar_event_t * event=&store.events[i];
            if(included(event)&&event->all_day&&same(event->date,hachimi_date_add_days(start,col)))
                button(lane,event->title,0,(int)row++*23,col_width-3,21,tints[event->category],colors[event->category],12,event_cb,event->id);
        }
    }
    lv_obj_t * scroll=panel(content,8,180,575,207,0xffffff,0);
    lv_obj_add_flag(scroll,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(scroll,LV_DIR_VER);
    for(int hour=0;hour<24;hour++) {
        char text[16];snprintf(text,sizeof(text),"%02d:00",hour);
        label(scroll,text,2,hour*48,48,23,12,MUTED);rule(scroll,55,hour*48+8,511,1);
    }
    for(int col=0;col<=columns;col++)rule(scroll,55+col*col_width,0,1,24*48);
    int first_hour=8;
    for(int col=0;col<columns;col++) {
        unsigned order[HACHIMI_CALENDAR_MAX_EVENTS],assigned[HACHIMI_CALENDAR_MAX_EVENTS];
        unsigned lane_end[HACHIMI_CALENDAR_MAX_EVENTS]={0},count=0,lane_count=0;
        for(unsigned i=0;i<store.count;i++) {
            const hachimi_calendar_event_t * event=&store.events[i];
            if(included(event)&&!event->all_day&&same(event->date,hachimi_date_add_days(start,col)))order[count++]=i;
        }
        for(unsigned i=1;i<count;i++) {
            unsigned item=order[i],j=i;
            while(j>0&&store.events[order[j-1]].start_minute>store.events[item].start_minute){order[j]=order[j-1];j--;}
            order[j]=item;
        }
        for(unsigned i=0;i<count;i++) {
            const hachimi_calendar_event_t * event=&store.events[order[i]];unsigned lane=0;
            while(lane<lane_count&&lane_end[lane]>event->start_minute)lane++;
            if(lane==lane_count)lane_count++;
            assigned[i]=lane;lane_end[lane]=event->end_minute;
        }
        for(unsigned i=0;i<count;i++) {
            const hachimi_calendar_event_t * event=&store.events[order[i]];
            int y=(int)event->start_minute*48/60+9,h=(int)(event->end_minute-event->start_minute)*48/60;
            if(h<26)h=26;
            if((int)(event->start_minute/60)<first_hour)first_hour=(int)(event->start_minute/60);
            char text[160],time_text[40];format_time(event,time_text,sizeof(time_text));
            snprintf(text,sizeof(text),"%s\n%s",event->title,time_text);
            int width=(col_width-5)/(int)lane_count;if(width<1)width=1;
            lv_obj_t * item=button(scroll,text,58+col*col_width+(int)assigned[i]*width,y,width>2?width-2:1,h,tints[event->category],colors[event->category],12,event_cb,event->id);
            lv_obj_set_style_border_color(item,lv_color_hex(colors[event->category]),0);
            lv_obj_set_style_border_width(item,2,0);lv_obj_set_style_border_side(item,LV_BORDER_SIDE_LEFT,0);
            lv_obj_t * caption=lv_obj_get_child(item,0);lv_obj_set_pos(caption,5,2);lv_obj_set_size(caption,width>14?width-12:1,h-2);
        }
    }
    if(hachimi_date_compare(today,start)>=0&&hachimi_date_compare(today,hachimi_date_add_days(start,columns-1))<=0) {
        time_t now=time(NULL);struct tm * local=localtime(&now);
        if(local) {
            int column=0;while(column<columns&&!same(hachimi_date_add_days(start,column),today))column++;
            lv_obj_t * line=panel(scroll,55+column*col_width,(local->tm_hour*60+local->tm_min)*48/60+8,col_width,2,RED,0);
            lv_obj_clear_flag(line,LV_OBJ_FLAG_CLICKABLE);
        }
    }
    lv_obj_update_layout(scroll);lv_obj_scroll_to_y(scroll,first_hour*48,LV_ANIM_OFF);
}
static bool matches(const hachimi_calendar_event_t * event)
{
    if(!included(event))return false;
    if(current_view==VIEW_SEARCH)return strstr(event->title,search_query)!=NULL||strstr(event->location,search_query)!=NULL;
    return hachimi_date_compare(event->date,selected)>=0;
}
static void render_list(void)
{
    if(results==NULL)return;
    lv_obj_clean(results);
    unsigned order[HACHIMI_CALENDAR_MAX_EVENTS],count=0;
    for(unsigned i=0;i<store.count;i++)if(matches(&store.events[i]))order[count++]=i;
    for(unsigned i=1;i<count;i++) {
        unsigned item=order[i],j=i;
        while(j>0) {
            const hachimi_calendar_event_t * a=&store.events[order[j-1]],*b=&store.events[item];
            int compare=hachimi_date_compare(a->date,b->date);
            if(compare<0||(compare==0&&a->start_minute<=b->start_minute))break;
            order[j]=order[j-1];j--;
        }order[j]=item;
    }
    if(count==0) {label(results,current_view==VIEW_SEARCH?"无搜索结果":"没有即将到来的日程",20,70,510,35,22,MUTED);return;}
    for(unsigned row=0;row<count;row++) {
        const hachimi_calendar_event_t * event=&store.events[order[row]];int y=(int)row*66;
        char text[160],time_text[40];format_time(event,time_text,sizeof(time_text));
        snprintf(text,sizeof(text),"%d月%d日 周%s",event->date.month,event->date.day,weekdays[hachimi_date_weekday(event->date)]);
        label(results,text,8,y+6,124,22,14,MUTED);
        lv_obj_t * item=button(results,event->title,139,y+3,404,34,0xffffff,INK,18,event_cb,event->id);
        lv_obj_t * caption=lv_obj_get_child(item,0);lv_obj_set_size(caption,380,28);lv_obj_set_pos(caption,8,2);
        panel(results,136,y+7,3,46,colors[event->category],2);
        snprintf(text,sizeof(text),"%s%s%s",time_text,event->location[0]?" · ":"",event->location);
        label(results,text,147,y+37,396,22,12,MUTED);rule(results,139,y+63,404,1);
    }
}
static void hide_keyboard(void)
{
    if(keyboard) {
        lv_keyboard_set_textarea(keyboard,NULL);
        lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
        hachimi_desktop_set_app_gestures_enabled(true);
    }
}
static void keyboard_cb(lv_event_t * event)
{
    if(lv_event_get_code(event)==LV_EVENT_READY||lv_event_get_code(event)==LV_EVENT_CANCEL)hide_keyboard();
}
static void show_keyboard(lv_obj_t * input)
{
    lv_keyboard_set_textarea(keyboard,input);
    lv_obj_clear_flag(keyboard,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(keyboard);
    hachimi_desktop_set_app_gestures_enabled(false);
}
static void focus_cb(lv_event_t * event) {show_keyboard(lv_event_get_target(event));}
static void search_cb(lv_event_t * event)
{
    snprintf(search_query,sizeof(search_query),"%s",lv_textarea_get_text(lv_event_get_target(event)));render_list();
}
static void close_editor(void)
{
    hide_keyboard();if(editor_overlay)lv_obj_delete(editor_overlay);editor=NULL;editor_overlay=NULL;
    title_input=location_input=editor_error=NULL;
}
static lv_obj_t * dropdown(lv_obj_t * parent,int x,int y,int w,const char * options,unsigned selected_value)
{
    lv_obj_t * obj=lv_dropdown_create(parent);lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,32);
    lv_dropdown_set_options(obj,options);lv_dropdown_set_selected(obj,selected_value);
    lv_obj_set_style_text_font(obj,hachimi_ui_font(14),0);lv_obj_set_style_radius(obj,7,0);
    lv_obj_set_style_text_font(obj,&lv_font_montserrat_14,LV_PART_INDICATOR);
    lv_obj_set_style_border_width(obj,0,0);lv_obj_set_style_bg_color(obj,lv_color_hex(SIDE),0);
    lv_obj_t * list=lv_dropdown_get_list(obj);lv_obj_set_style_text_font(list,hachimi_ui_font(14),0);
    lv_obj_set_style_max_height(list,175,0);return obj;
}
static void number_options(char * text,size_t capacity,int first,int last,bool padded)
{
    size_t used=0;text[0]='\0';
    for(int i=first;i<=last;i++) {
        int count=snprintf(text+used,capacity-used,padded?"%s%02d":"%s%d",i==first?"":"\n",i);
        if(count<0||(size_t)count>=capacity-used)break;
        used+=(size_t)count;
    }
}
static void editor_date_cb(lv_event_t * event)
{
    (void)event;unsigned day=lv_dropdown_get_selected(date_fields[2]);
    int year=(int)lv_dropdown_get_selected(date_fields[0])+1900;
    int month=(int)lv_dropdown_get_selected(date_fields[1])+1;
    int maximum=hachimi_date_month_days(year,month);char options[128];number_options(options,sizeof(options),1,maximum,false);
    lv_dropdown_set_options(date_fields[2],options);lv_dropdown_set_selected(date_fields[2],day<(unsigned)maximum?day:(unsigned)maximum-1);
}
static void all_day_cb(lv_event_t * event)
{
    (void)event;bool all=lv_obj_has_state(all_day_switch,LV_STATE_CHECKED);
    for(unsigned i=0;i<4;i++) {if(all)lv_obj_add_state(time_fields[i],LV_STATE_DISABLED);else lv_obj_remove_state(time_fields[i],LV_STATE_DISABLED);}
}
static void save_event_cb(lv_event_t * event)
{
    (void)event;hide_keyboard();hachimi_calendar_event_t item={0};item.id=editing_id;
    snprintf(item.title,sizeof(item.title),"%s",lv_textarea_get_text(title_input));
    snprintf(item.location,sizeof(item.location),"%s",lv_textarea_get_text(location_input));
    item.date.year=(int)lv_dropdown_get_selected(date_fields[0])+1900;
    item.date.month=(int)lv_dropdown_get_selected(date_fields[1])+1;item.date.day=(int)lv_dropdown_get_selected(date_fields[2])+1;
    item.all_day=lv_obj_has_state(all_day_switch,LV_STATE_CHECKED);item.category=lv_dropdown_get_selected(category_dropdown);
    item.start_minute=lv_dropdown_get_selected(time_fields[0])*60+lv_dropdown_get_selected(time_fields[1]);
    item.end_minute=lv_dropdown_get_selected(time_fields[2])*60+lv_dropdown_get_selected(time_fields[3]);
    if(item.all_day) {item.start_minute=0;item.end_minute=1440;}
    if(!item.title[0]) {lv_label_set_text(editor_error,"请输入标题");return;}
    if(item.end_minute<=item.start_minute||item.end_minute>1440) {lv_label_set_text(editor_error,"结束时间须晚于开始时间");return;}
    if(!hachimi_calendar_put(&store,&item)) {lv_label_set_text(editor_error,"无法保存日程，请检查存储空间或日程数量");return;}
    selected=item.date;close_editor();render();
}
static void cancel_cb(lv_event_t * event) {(void)event;close_editor();}
static void delete_confirm_cb(lv_event_t * event)
{
    uintptr_t action=(uintptr_t)lv_event_get_user_data(event);
    if(action==0) {lv_obj_delete(lv_obj_get_parent(lv_event_get_target(event)));return;}
    if(!hachimi_calendar_delete(&store,editing_id)) {lv_label_set_text(editor_error,"无法删除日程");return;}
    close_editor();render();
}
static void delete_cb(lv_event_t * event)
{
    (void)event;hide_keyboard();
    lv_obj_t * confirmation=panel(editor,60,160,430,150,SIDE,16);
    label(confirmation,"删除这个日程？",20,18,390,32,22,INK);
    button(confirmation,"取消",20,89,180,40,0xffffff,INK,16,delete_confirm_cb,0);
    button(confirmation,"删除",230,89,180,40,RED,0xffffff,16,delete_confirm_cb,1);
}
static lv_obj_t * text_input(lv_obj_t * parent,const char * placeholder,const char * text,int y)
{
    lv_obj_t * input=lv_textarea_create(parent);lv_obj_set_pos(input,22,y);lv_obj_set_size(input,506,38);
    lv_textarea_set_one_line(input,true);lv_textarea_set_max_length(input,32);
    lv_textarea_set_placeholder_text(input,placeholder);lv_textarea_set_text(input,text);
    lv_obj_set_style_text_font(input,hachimi_ui_font(18),0);lv_obj_set_style_bg_color(input,lv_color_hex(SIDE),0);
    lv_obj_set_style_border_width(input,0,0);lv_obj_set_style_radius(input,8,0);
    lv_obj_add_event_cb(input,focus_cb,LV_EVENT_FOCUSED,NULL);
    lv_obj_add_event_cb(input,focus_cb,LV_EVENT_CLICKED,NULL);return input;
}
static void show_editor(uint32_t id)
{
    if(editor)close_editor();
    hide_keyboard();
    hachimi_calendar_event_t item={0};item.date=selected;item.start_minute=9*60;item.end_minute=10*60;
    if(id) {bool found=false;for(unsigned i=0;i<store.count;i++)if(store.events[i].id==id){item=store.events[i];found=true;break;}if(!found)return;}
    editing_id=id;
    lv_obj_t * overlay=panel(root,0,0,800,480,0x000000,0);lv_obj_set_style_bg_opa(overlay,65,0);
    editor_overlay=overlay;
    editor=panel(overlay,216,48,550,390,0xffffff,18);
    button(editor,"取消",14,12,64,32,0xffffff,RED,16,cancel_cb,0);
    lv_obj_t * heading=label(editor,id?"编辑日程":"新建日程",150,14,250,32,22,INK);lv_obj_set_style_text_align(heading,LV_TEXT_ALIGN_CENTER,0);
    button(editor,"存储",466,12,64,32,0xffffff,RED,16,save_event_cb,0);
    title_input=text_input(editor,"标题",item.title,60);location_input=text_input(editor,"位置",item.location,106);
    label(editor,"日期",22,163,56,24,14,MUTED);
    char options[1600];number_options(options,sizeof(options),1900,2199,false);
    date_fields[0]=dropdown(editor,78,157,100,options,(unsigned)item.date.year-1900);
    number_options(options,sizeof(options),1,12,false);date_fields[1]=dropdown(editor,184,157,65,options,(unsigned)item.date.month-1);
    number_options(options,sizeof(options),1,hachimi_date_month_days(item.date.year,item.date.month),false);
    date_fields[2]=dropdown(editor,255,157,65,options,(unsigned)item.date.day-1);
    lv_obj_add_event_cb(date_fields[0],editor_date_cb,LV_EVENT_VALUE_CHANGED,NULL);lv_obj_add_event_cb(date_fields[1],editor_date_cb,LV_EVENT_VALUE_CHANGED,NULL);
    label(editor,"全天",374,163,60,24,14,INK);all_day_switch=lv_switch_create(editor);
    lv_obj_set_pos(all_day_switch,460,157);lv_obj_set_size(all_day_switch,54,30);
    lv_obj_set_style_bg_color(all_day_switch,lv_color_hex(0x34c759),LV_PART_INDICATOR|LV_STATE_CHECKED);
    if(item.all_day)lv_obj_add_state(all_day_switch,LV_STATE_CHECKED);
    lv_obj_add_event_cb(all_day_switch,all_day_cb,LV_EVENT_VALUE_CHANGED,NULL);
    label(editor,"开始",22,211,60,24,14,MUTED);label(editor,"结束",286,211,60,24,14,MUTED);
    unsigned values[]={item.start_minute/60,item.start_minute%60,item.end_minute/60,item.end_minute%60};
    const int positions[]={78,154,342,418};
    for(unsigned i=0;i<4;i++) {
        number_options(options,sizeof(options),0,(i%2)?59:(i==2?24:23),true);
        time_fields[i]=dropdown(editor,positions[i],205,70,options,values[i]);
    }
    all_day_cb(NULL);label(editor,"日历",22,261,60,24,14,MUTED);
    category_dropdown=dropdown(editor,78,253,176,"个人\n工作\n家庭",item.category);
    if(id)button(editor,"删除日程",360,253,155,36,0xffeeec,RED,14,delete_cb,0);
    rule(editor,22,310,506,1);
    label(editor,"日程保存在此设备",22,326,506,22,14,MUTED);
    editor_error=label(editor,"",22,353,506,24,14,RED);
    lv_obj_move_foreground(keyboard);
}
static void search_open_cb(lv_event_t * event)
{
    (void)event;current_view=VIEW_SEARCH;search_query[0]='\0';render();
    show_keyboard(search_input);
}
static void render(void)
{
    if(!content)return;
    hide_keyboard();results=NULL;search_input=NULL;
    lv_obj_clean(content);render_sidebar();
    const char * views[]={"日","周","月","年"};
    lv_obj_t * segment=panel(content,155,4,232,34,0xededf0,9);
    for(unsigned i=0;i<4;i++)button(segment,views[i],2+(int)i*58,2,55,30,current_view==(calendar_view_t)i?0xffffff:0xededf0,
                                   INK,16,view_cb,i);
    button(content,"列表",398,4,54,34,0xffffff,current_view==VIEW_LIST?RED:MUTED,14,view_cb,VIEW_LIST);
    button(content,"搜索",455,4,54,34,0xffffff,RED,14,search_open_cb,0);
    button(content,"+",521,1,50,38,0xffffff,RED,32,event_cb,0);
    char title[64];
    if(current_view==VIEW_YEAR)snprintf(title,sizeof(title),"%d年",selected.year);
    else if(current_view==VIEW_DAY)snprintf(title,sizeof(title),"%d年%d月%d日",selected.year,selected.month,selected.day);
    else snprintf(title,sizeof(title),"%d年%d月",selected.year,selected.month);
    label(content,title,16,49,360,42,28,INK);
    button(content,"今天",394,52,70,32,0xffffff,RED,14,navigate_cb,0);
    button(content,"‹",470,48,46,38,0xffffff,RED,28,navigate_cb,(uintptr_t)(intptr_t)-1);
    button(content,"›",526,48,46,38,0xffffff,RED,28,navigate_cb,1);
    if(current_view==VIEW_MONTH)render_month();
    else if(current_view==VIEW_YEAR)render_year();
    else if(current_view==VIEW_DAY||current_view==VIEW_WEEK)render_timeline();
    else {
        int y=98;
        if(current_view==VIEW_SEARCH) {
            search_input=lv_textarea_create(content);lv_obj_set_pos(search_input,16,96);lv_obj_set_size(search_input,550,36);
            lv_textarea_set_one_line(search_input,true);lv_textarea_set_max_length(search_input,32);
            lv_textarea_set_placeholder_text(search_input,"搜索标题或位置");lv_textarea_set_text(search_input,search_query);
            lv_obj_set_style_text_font(search_input,hachimi_ui_font(16),0);
            lv_obj_add_event_cb(search_input,search_cb,LV_EVENT_VALUE_CHANGED,NULL);lv_obj_add_event_cb(search_input,focus_cb,LV_EVENT_FOCUSED,NULL);
            lv_obj_add_event_cb(search_input,focus_cb,LV_EVENT_CLICKED,NULL);
            y=143;
        }
        results=panel(content,16,y,560,387-y,0xffffff,0);lv_obj_add_flag(results,LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(results,LV_DIR_VER);render_list();
    }
    if(!store.writable)label(content,"日程文件无法读取，已保留原文件",18,364,550,24,14,RED);
}
static void refresh_cb(lv_timer_t * timer)
{
    (void)timer;hachimi_date_t date=local_date();if(same(date,today))return;
    if(same(selected,today))selected=date;
    today=date;if(!editor)render();
}
static void root_deleted(lv_event_t * event)
{
    (void)event;if(refresh_timer)lv_timer_delete(refresh_timer);refresh_timer=NULL;
    root=content=sidebar=keyboard=editor=editor_overlay=results=search_input=NULL;
}
static lv_obj_t * create(lv_obj_t * parent)
{
    (void)hachimi_calendar_load(&store);today=local_date();selected=today;current_view=VIEW_MONTH;
    for(unsigned i=0;i<3;i++)visible_categories[i]=true;
    root=panel(parent,0,0,800,480,0xffffff,0);
    sidebar=panel(root,0,0,194,480,SIDE,0);rule(root,193,0,1,480);
    content=panel(root,200,54,590,390,0xffffff,0);
    keyboard=lv_keyboard_create(root);lv_obj_set_align(keyboard,LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(keyboard,0,264);lv_obj_set_size(keyboard,800,216);
    lv_obj_set_style_text_font(keyboard,&lv_font_montserrat_14,0);
    lv_obj_add_event_cb(keyboard,keyboard_cb,LV_EVENT_READY,NULL);lv_obj_add_event_cb(keyboard,keyboard_cb,LV_EVENT_CANCEL,NULL);
    lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);render();ios_status_bar_create(root,lv_color_hex(INK));
    lv_obj_add_event_cb(root,root_deleted,LV_EVENT_DELETE,NULL);refresh_timer=lv_timer_create(refresh_cb,60000,NULL);
    lv_obj_add_flag(root,LV_OBJ_FLAG_HIDDEN);return root;
}
static void opened(lv_obj_t * view) {(void)view;refresh_cb(NULL);render();}
static void closed(lv_obj_t * view) {(void)view;close_editor();hide_keyboard();}
const hachimi_app_module_t hachimi_calendar_app={"calendar",create,opened,closed,NULL,NULL};
