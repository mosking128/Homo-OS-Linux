#include "maps_app.h"
#include "framework/desktop.h"
#include "framework/ui/fonts.h"
#include "services/offline_map.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BLUE 0x007aff
#define INK 0x263343
#define MUTED 0x81909e
#define MAP_X 66
#define MAP_W (800-MAP_X)
#define MAP_H 480
#define RESULT_COUNT 24
static lv_obj_t * root,* view,* list,* search,* keyboard,* caption,* detail,* scale_label,* clock_label;
static lv_obj_t * chips[3],* plus,* minus;
static lv_timer_t * timer;
static hachimi_map_t * map;
static lv_image_dsc_t tile_images[HACHIMI_MAP_CACHE_SIZE];
static double center_x,center_y;
static int zoom=15,pending_zoom;
static double pending_x,pending_y;
static unsigned results[RESULT_COUNT],result_count,selected=UINT32_MAX;
static unsigned filter;
static uint32_t cache_version;
static bool active,dragged;
static int drag_distance;
static lv_point_t press_point;
static const char * const filters[]={"","景点","公园"};
static void render_results(void);
static void update_scale(void);

/* The list is opaque. Repainting it on every drag wastes most of the frame on
 * text and rounded cards. Refresh the exposed map plus the panel's rounded
 * corners/shadow; LVGL still invalidates the list normally when its content
 * changes. These four bands also cover the right-hand floating controls. */
static void invalidate_map(void)
{
    if(!view)return;
    lv_area_t bounds;lv_obj_get_coords(view,&bounds);
    int x=bounds.x1-MAP_X,y=bounds.y1;
    const lv_area_t exposed[]={
        {x+348,y,x+799,y+479},{x+66,y,x+85,y+479},
        {x+86,y,x+347,y+39},{x+86,y+438,x+347,y+479}
    };
    for(unsigned i=0;i<sizeof(exposed)/sizeof(exposed[0]);i++)lv_obj_invalidate_area(view,&exposed[i]);
}

static lv_obj_t * label(lv_obj_t * parent,const char * text,int x,int y,int w,int h,unsigned size,uint32_t color)
{
    lv_obj_t * obj=lv_label_create(parent);lv_label_set_text(obj,text);
    lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,h);
    lv_obj_set_style_text_font(obj,hachimi_ui_font(size),0);lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
    lv_label_set_long_mode(obj,LV_LABEL_LONG_MODE_DOTS);
    lv_obj_clear_flag(obj,LV_OBJ_FLAG_CLICKABLE);return obj;
}
static lv_obj_t * panel(lv_obj_t * parent,int x,int y,int w,int h,int radius,uint32_t color)
{
    lv_obj_t * obj=lv_obj_create(parent);lv_obj_remove_style_all(obj);lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,h);
    lv_obj_set_style_radius(obj,radius,0);lv_obj_set_style_bg_color(obj,lv_color_hex(color),0);
    lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE);return obj;
}
static lv_obj_t * button(lv_obj_t * parent,const char * text,int x,int y,int w,int h,bool symbol,lv_event_cb_t cb,intptr_t data)
{
    lv_obj_t * obj=lv_button_create(parent);lv_obj_remove_style_all(obj);lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,h);
    lv_obj_set_style_radius(obj,12,0);lv_obj_set_style_bg_color(obj,lv_color_white(),0);lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(obj,lv_color_hex(0xe2edfb),LV_STATE_PRESSED);
    lv_obj_t * text_obj=lv_label_create(obj);lv_label_set_text(text_obj,text);
    lv_obj_set_style_text_font(text_obj,symbol?&lv_font_montserrat_20:hachimi_ui_font(14),0);
    lv_obj_set_style_text_color(text_obj,lv_color_hex(BLUE),0);lv_obj_center(text_obj);
    lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_GESTURE_BUBBLE);
    if(cb)lv_obj_add_event_cb(obj,cb,LV_EVENT_CLICKED,(void*)data);
    return obj;
}
static void clamp_center(void){hachimi_map_clamp(map,zoom,MAP_W,MAP_H,&center_x,&center_y);}
static void set_center(double lat,double lon)
{
    hachimi_map_world(lat,lon,zoom,&center_x,&center_y);clamp_center();
    pending_zoom=0;invalidate_map();update_scale();
}
static void hide_keyboard(void)
{
    lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);lv_keyboard_set_textarea(keyboard,NULL);
    lv_obj_remove_state(search,LV_STATE_FOCUSED);
}
static void select_result(lv_event_t * event)
{
    unsigned index=(unsigned)(uintptr_t)lv_event_get_user_data(event);
    const hachimi_map_place_t * p=hachimi_map_place(map,index);if(!p)return;
    selected=index;hide_keyboard();set_center(p->lat,p->lon);
    /* The list covers the left part of the map. Offset the view centre so the
     * chosen place sits in the uncovered right-hand map, not behind the list. */
    center_x-=135;clamp_center();
    lv_label_set_text_fmt(detail,"%s · %s",p->category,p->name);render_results();
}
static void render_results(void)
{
    if(!list)return;
    lv_obj_clean(list);
    double lat,lon;hachimi_map_coords(center_x,center_y,zoom,&lat,&lon);
    result_count=hachimi_map_search(map,lv_textarea_get_text(search),filters[filter],lat,lon,results,RESULT_COUNT);
    for(unsigned i=0;i<result_count;i++) {
        const hachimi_map_place_t * p=hachimi_map_place(map,results[i]);
        lv_obj_t * row=lv_button_create(list);lv_obj_remove_style_all(row);lv_obj_set_size(row,246,64);
        lv_obj_set_style_radius(row,14,0);lv_obj_set_style_bg_opa(row,LV_OPA_COVER,0);
        lv_obj_set_style_bg_color(row,lv_color_hex(selected==results[i]?0xe3efff:0xf0f4f8),0);
        lv_obj_set_style_bg_color(row,lv_color_hex(0xdceaff),LV_STATE_PRESSED);
        lv_obj_clear_flag(row,LV_OBJ_FLAG_SCROLLABLE);
        label(row,p->name,14,7,213,25,18,INK);
        double d=hachimi_map_distance(lat,lon,p->lat,p->lon);char info[96];
        if(d>=1000)snprintf(info,sizeof(info),"%s  ·  距中心 %.1f 公里",p->category,d/1000.0);
        else snprintf(info,sizeof(info),"%s  ·  距中心 %.0f 米",p->category,d);
        label(row,info,14,35,213,19,12,MUTED);
        lv_obj_add_event_cb(row,select_result,LV_EVENT_CLICKED,(void*)(uintptr_t)results[i]);
    }
    if(!result_count) label(list,map?"没有匹配地点\n试试拼音或首字母":"未找到北京地图数据包",12,16,230,65,14,MUTED);
    lv_label_set_text_fmt(caption,"%u 个地点  ·  按地图中心排序",result_count);
    lv_obj_scroll_to_y(list,0,LV_ANIM_OFF);
    for(unsigned i=0;i<3;i++)lv_obj_set_style_bg_color(chips[i],lv_color_hex(filter==i?0xdceaff:0xf0f4f8),0);
}
static void filter_event(lv_event_t * event)
{
    filter=(unsigned)(uintptr_t)lv_event_get_user_data(event);if(filter>2)filter=0;
    hide_keyboard();render_results();
}
static void search_event(lv_event_t * event)
{
    lv_event_code_t code=lv_event_get_code(event);
    if(code==LV_EVENT_VALUE_CHANGED)render_results();
    else if(code==LV_EVENT_PRESSED||code==LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(keyboard,search);lv_obj_clear_flag(keyboard,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(keyboard);
    }
}
static void keyboard_event(lv_event_t * event)
{
    lv_event_code_t code=lv_event_get_code(event);if(code==LV_EVENT_READY||code==LV_EVENT_CANCEL)hide_keyboard();
}
static void clear_search(lv_event_t * event){(void)event;lv_textarea_set_text(search,"");hide_keyboard();render_results();}
static void home_event(lv_event_t * event){(void)event;hachimi_desktop_home();}
static void center_event(lv_event_t * event)
{
    (void)event;if(!map)return;double lat,lon;hachimi_map_center(map,&lat,&lon);set_center(lat,lon);
    selected=UINT32_MAX;lv_label_set_text(detail,"北京中心城区 · 拖动浏览");render_results();
}
static bool request_tiles(int z,double x,double y)
{
    double origin_x=x-MAP_W/2.0,origin_y=y-MAP_H/2.0;bool all=true;
    int x0=(int)floor(origin_x/256),y0=(int)floor(origin_y/256);
    int x1=(int)floor((origin_x+MAP_W-1)/256),y1=(int)floor((origin_y+MAP_H-1)/256);
    for(int ty=y0;ty<=y1;ty++)for(int tx=x0;tx<=x1;tx++){bool settled;hachimi_map_tile(map,z,tx,ty,NULL,&settled);if(!settled)all=false;}
    return all;
}
static void zoom_event(lv_event_t * event)
{
    if(!map)return;
    int requested=(pending_zoom?pending_zoom:zoom)+(int)(intptr_t)lv_event_get_user_data(event);
    if(requested<13||requested>16)return;
    double lat,lon;hachimi_map_coords(center_x+135,center_y,zoom,&lat,&lon);
    pending_zoom=requested;hachimi_map_world(lat,lon,pending_zoom,&pending_x,&pending_y);
    pending_x-=135;
    hachimi_map_clamp(map,pending_zoom,MAP_W,MAP_H,&pending_x,&pending_y);
    hachimi_map_begin_frame(map);request_tiles(pending_zoom,pending_x,pending_y);
    lv_label_set_text(detail,"正在读取比例尺，保留当前地图…");
}
static void update_scale(void)
{
    if(!scale_label)return;
    double lat,lon;hachimi_map_coords(center_x,center_y,zoom,&lat,&lon);(void)lon;
    double meters=156543.03392*cos(lat*3.14159265358979323846/180)/(1U<<zoom)*80;
    char text[32];
    if(meters>=1000)snprintf(text,sizeof(text),"%.1f km",meters/1000);
    else snprintf(text,sizeof(text),"%.0f m",meters);
    lv_label_set_text(scale_label,text);
    lv_obj_set_state(plus,LV_STATE_DISABLED,zoom>=16);lv_obj_set_state(minus,LV_STATE_DISABLED,zoom<=13);
}
static void draw_map(lv_event_t * event)
{
    if(!map)return;
    lv_layer_t * layer=lv_event_get_layer(event);lv_area_t area;lv_obj_get_coords(view,&area);
    double ox=center_x-MAP_W/2.0,oy=center_y-MAP_H/2.0;
    int x0=(int)floor(ox/256),y0=(int)floor(oy/256);
    int x1=(int)floor((ox+MAP_W-1)/256),y1=(int)floor((oy+MAP_H-1)/256);
    hachimi_map_begin_frame(map);
    lv_draw_image_dsc_t dsc;lv_draw_image_dsc_init(&dsc);dsc.antialias=false;
    for(int ty=y0;ty<=y1;ty++)for(int tx=x0;tx<=x1;tx++) {
        int slot;const uint8_t * pixels=hachimi_map_tile(map,zoom,tx,ty,&slot,NULL);if(!pixels)continue;
        lv_image_dsc_t * image=&tile_images[slot];image->data=pixels;
        int px=area.x1+(int)lround(tx*256.0-ox),py=area.y1+(int)lround(ty*256.0-oy);
        lv_area_t rect={px,py,px+255,py+255};dsc.src=image;lv_draw_image(layer,&dsc,&rect);
    }
    const hachimi_map_place_t * p=hachimi_map_place(map,selected);
    if(p) {
        double wx,wy;hachimi_map_world(p->lat,p->lon,zoom,&wx,&wy);
        int x=area.x1+(int)lround(wx-ox),y=area.y1+(int)lround(wy-oy);
        lv_draw_rect_dsc_t dot;lv_draw_rect_dsc_init(&dot);dot.radius=LV_RADIUS_CIRCLE;
        dot.bg_color=lv_color_hex(BLUE);dot.bg_opa=LV_OPA_20;
        lv_area_t halo={x-19,y-19,x+19,y+19};lv_draw_rect(layer,&dot,&halo);
        dot.bg_opa=LV_OPA_COVER;dot.border_color=lv_color_white();dot.border_width=3;
        lv_area_t point={x-10,y-10,x+10,y+10};lv_draw_rect(layer,&dot,&point);
    }
}
static void map_event(lv_event_t * event)
{
    lv_event_code_t code=lv_event_get_code(event);
    if(code==LV_EVENT_DRAW_MAIN){draw_map(event);return;}
    lv_indev_t * input=lv_indev_active();if(!input||!map)return;
    if(code==LV_EVENT_PRESSED){lv_indev_get_point(input,&press_point);dragged=false;drag_distance=0;hide_keyboard();}
    else if(code==LV_EVENT_PRESSING) {
        lv_point_t point;lv_indev_get_point(input,&point);int dx=point.x-press_point.x,dy=point.y-press_point.y;press_point=point;
        drag_distance+=abs(dx)+abs(dy);if(drag_distance>5)dragged=true;
        if(dragged&&(dx||dy)){center_x-=dx;center_y-=dy;clamp_center();pending_zoom=0;invalidate_map();update_scale();}
    }
    else if(code==LV_EVENT_RELEASED&&dragged) {render_results();lv_label_set_text(detail,"北京中心城区 · 拖动浏览");}
}
static void poll(lv_timer_t * t)
{
    (void)t;if(!active)return;
    hachimi_map_stats_t stats;hachimi_map_stats(map,&stats);
    if(pending_zoom) {
        hachimi_map_begin_frame(map);
        /* Keep the displayed level pinned while staging a new one. */
        request_tiles(zoom,center_x,center_y);
        if(request_tiles(pending_zoom,pending_x,pending_y)) {
            zoom=pending_zoom;pending_zoom=0;center_x=pending_x;center_y=pending_y;
            invalidate_map();update_scale();lv_label_set_text(detail,"北京中心城区 · 拖动浏览");
        }
    }
    if(cache_version!=stats.version){cache_version=stats.version;invalidate_map();}
    time_t now=time(NULL);struct tm * local=localtime(&now);char text[16];
    if(local){strftime(text,sizeof(text),"%H:%M",local);if(strcmp(lv_label_get_text(clock_label),text))lv_label_set_text(clock_label,text);}
}
static void deleted(lv_event_t * event)
{
    (void)event;active=false;if(timer){lv_timer_delete(timer);timer=NULL;}
    hachimi_map_close(map);map=NULL;root=view=list=search=keyboard=caption=detail=scale_label=clock_label=NULL;
}
static lv_obj_t * create(lv_obj_t * parent)
{
    zoom=15;pending_zoom=0;filter=0;selected=UINT32_MAX;cache_version=0;active=false;
    memset(tile_images,0,sizeof(tile_images));
    for(unsigned i=0;i<HACHIMI_MAP_CACHE_SIZE;i++) {
        tile_images[i].header.magic=LV_IMAGE_HEADER_MAGIC;tile_images[i].header.cf=LV_COLOR_FORMAT_RGB565;
        tile_images[i].header.w=tile_images[i].header.h=256;tile_images[i].header.stride=512;tile_images[i].data_size=HACHIMI_MAP_TILE_BYTES;
    }
    const char * directory=getenv("HACHIMI_MAPS_DIR");if(!directory||!*directory)directory="assets/maps/beijing";
    map=hachimi_map_open(directory);
    double lat=39.9042,lon=116.4074;hachimi_map_center(map,&lat,&lon);hachimi_map_world(lat,lon,zoom,&center_x,&center_y);clamp_center();
    root=panel(parent,0,0,800,480,0,0xf2f5f8);lv_obj_add_event_cb(root,deleted,LV_EVENT_DELETE,NULL);
    view=panel(root,MAP_X,0,MAP_W,MAP_H,0,0xf4f2ed);
    lv_obj_add_flag(view,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_clear_flag(view,LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(view,map_event,LV_EVENT_ALL,NULL);
    panel(root,0,0,66,480,0,0xe8eef5);
    clock_label=label(root,"--:--",8,22,54,25,16,INK);
    lv_obj_t * logo=lv_image_create(root);lv_image_set_src(logo,"A:assets/ios/map.png");
    lv_image_set_scale(logo,160);lv_image_set_pivot(logo,0,0);lv_obj_set_pos(logo,13,147);
    button(root,LV_SYMBOL_LIST,12,211,42,44,true,filter_event,0);
    button(root,LV_SYMBOL_GPS,12,269,42,44,true,center_event,0);
    button(root,LV_SYMBOL_HOME,12,410,42,44,true,home_event,0);
    lv_obj_t * sidebar=panel(root,78,14,278,452,23,0xfbfcfe);
    lv_obj_set_style_shadow_width(sidebar,8,0);lv_obj_set_style_shadow_opa(sidebar,LV_OPA_10,0);
    lv_obj_set_style_shadow_color(sidebar,lv_color_hex(0x526575),0);
    label(sidebar,"北京",18,16,130,32,28,INK);
    lv_obj_t * box=panel(sidebar,14,65,250,42,13,0xeef2f7);
    lv_obj_t * lens=panel(box,13,13,11,11,LV_RADIUS_CIRCLE,0xeef2f7);
    lv_obj_set_style_border_width(lens,2,0);lv_obj_set_style_border_color(lens,lv_color_hex(MUTED),0);
    lv_obj_clear_flag(lens,LV_OBJ_FLAG_CLICKABLE);
    static const lv_point_precise_t handle_points[]={{0,0},{5,5}};
    lv_obj_t * handle=lv_line_create(box);lv_line_set_points(handle,handle_points,2);lv_obj_set_pos(handle,22,22);
    lv_obj_set_style_line_width(handle,2,0);lv_obj_set_style_line_color(handle,lv_color_hex(MUTED),0);
    search=lv_textarea_create(box);lv_obj_remove_style_all(search);lv_obj_set_pos(search,36,9);lv_obj_set_size(search,179,25);
    lv_obj_set_style_text_font(search,hachimi_ui_font(16),0);lv_obj_set_style_text_color(search,lv_color_hex(INK),0);
    lv_textarea_set_one_line(search,true);lv_obj_set_height(search,25);lv_textarea_set_max_length(search,64);
    lv_textarea_set_placeholder_text(search,"地点 / 拼音搜索");lv_textarea_set_text(search,"");
    lv_obj_set_style_bg_color(search,lv_color_hex(BLUE),LV_PART_CURSOR);lv_obj_set_style_bg_opa(search,LV_OPA_COVER,LV_PART_CURSOR|LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(search,LV_OPA_TRANSP,LV_PART_CURSOR);lv_obj_set_scrollbar_mode(search,LV_SCROLLBAR_MODE_OFF);
    button(box,LV_SYMBOL_CLOSE,218,6,28,30,true,clear_search,0);
    for(unsigned i=0;i<3;i++)chips[i]=button(sidebar,i==0?"全部":filters[i],14+(int)i*86,119,78,32,false,filter_event,i);
    caption=label(sidebar,"",18,162,245,20,12,MUTED);
    list=panel(sidebar,14,190,250,211,0,0xfbfcfe);
    lv_obj_add_flag(list,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_flex_flow(list,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list,8,0);lv_obj_set_scrollbar_mode(list,LV_SCROLLBAR_MODE_OFF);
    detail=label(sidebar,map?"北京中心城区 · 拖动浏览":"请安装北京地图数据包",18,416,245,22,12,MUTED);
    lv_obj_t * top=panel(root,508,17,211,32,16,0xfdfefe);
    label(top,hachimi_map_region(map),14,7,195,23,14,INK);
    button(root,"N",736,17,48,42,false,center_event,0);
    plus=button(root,LV_SYMBOL_PLUS,736,77,48,48,true,zoom_event,1);
    minus=button(root,LV_SYMBOL_MINUS,736,128,48,48,true,zoom_event,-1);
    button(root,LV_SYMBOL_GPS,736,191,48,48,true,center_event,0);
    lv_obj_t * scale=panel(root,689,394,95,42,9,0xfdfefe);
    scale_label=label(scale,"",7,3,84,20,12,INK);
    panel(scale,7,27,80,2,0,INK);panel(scale,7,22,2,7,0,INK);panel(scale,85,22,2,7,0,INK);
    label(root,"© OpenStreetMap contributors",447,451,345,18,12,0x526575);
    keyboard=lv_keyboard_create(root);lv_obj_set_align(keyboard,LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(keyboard,374,250);lv_obj_set_size(keyboard,410,216);
    lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);lv_obj_add_event_cb(keyboard,keyboard_event,LV_EVENT_ALL,NULL);
    lv_obj_add_event_cb(search,search_event,LV_EVENT_VALUE_CHANGED,NULL);
    lv_obj_add_event_cb(search,search_event,LV_EVENT_PRESSED,NULL);lv_obj_add_event_cb(search,search_event,LV_EVENT_FOCUSED,NULL);
    render_results();update_scale();timer=lv_timer_create(poll,24,NULL);lv_timer_pause(timer);
    return root;
}
static void open_app(lv_obj_t * obj)
{
    (void)obj;active=true;if(timer)lv_timer_resume(timer);if(view)lv_obj_invalidate(view);poll(NULL);
}
static void close_app(lv_obj_t * obj)
{
    (void)obj;active=false;pending_zoom=0;hide_keyboard();if(timer)lv_timer_pause(timer);
}
const hachimi_app_module_t hachimi_maps_app={"maps",create,open_app,close_app,NULL,NULL};
