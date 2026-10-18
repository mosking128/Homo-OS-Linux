#include "music_app.h"
#include "framework/ui/fonts.h"
#include "framework/ui/ios_status_bar.h"
#include "services/music.h"
#include "draw/lv_image_decoder_private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define BG 0x202020
#define WHITE 0xd9d9d9
#define MUTED 0x858585
#define ACCENT 0xbac9c4
static lv_obj_t * root, * cover, * cover_placeholder, * title, * subtitle, * elapsed, * remaining;
static lv_obj_t * seek, * play_button, * lyrics_panel, * list_panel, * queue, * queue_title;
static lv_obj_t * lyric_labels[5], * error_label, * volume_panel, * volume_text, * mode_text;
static lv_obj_t * lyrics_button, * list_button;
static lv_obj_t * player_group[16];
static int player_group_x[16];
static unsigned player_group_count;
static int player_offset, player_offset_from, player_offset_target;
static uint32_t player_offset_started;
static bool player_offset_active;
static lv_timer_t * view_timer, * system_timer, * theme_timer, * layout_timer;
static hachimi_lrc_t * lrc;
static size_t loaded_track=(size_t)-1;
static int lyric_line=-2;
static bool last_playing, show_list, show_lyrics=true;
static char cover_path[1100];
static lv_color_t background, theme_from, theme_target;
static uint32_t theme_colors[HACHIMI_MUSIC_MAX];
static bool theme_cached[HACHIMI_MUSIC_MAX];
static uint32_t theme_started;
static bool theme_active;
static lv_obj_t * background_image;
#define PLAYER_CENTER_OFFSET 184
#define PLAYER_LAYOUT_DURATION 420
/* Ordered dithering avoids visible stripes on the board's RGB565 panel.
 * Only eight pixels are calculated per row; the pattern is copied across. */
static uint16_t background_pixels[800*480];
static const lv_image_dsc_t background_dsc={
    .header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=800,.h=480,.stride=1600},
    .data_size=sizeof(background_pixels),.data=(const uint8_t *)background_pixels
};
static void render_background(lv_color_t color)
{
    static const uint8_t bayer[8][8]={
        {0,48,12,60,3,51,15,63},{32,16,44,28,35,19,47,31},
        {8,56,4,52,11,59,7,55},{40,24,36,20,43,27,39,23},
        {2,50,14,62,1,49,13,61},{34,18,46,30,33,17,45,29},
        {10,58,6,54,9,57,5,53},{42,26,38,22,41,25,37,21}
    };
    for(unsigned y=0;y<480;y++) {
        unsigned shade=256-y*108/479;
        unsigned r=color.red*shade*31*64/(255*256),g=color.green*shade*63*64/(255*256),b=color.blue*shade*31*64/(255*256);
        uint16_t * row=background_pixels+y*800;
        for(unsigned x=0;x<8;x++) {
            unsigned d=bayer[y&7][x];
            row[x]=((r+d)>>6)<<11 | ((g+d)>>6)<<5 | ((b+d)>>6);
        }
        for(unsigned x=8;x<800;x+=8) memcpy(row+x,row,16);
    }
    lv_obj_invalidate(background_image);
}

/* Sample the decoded PNG, ignoring near-black borders and white lettering.
 * A weighted RGB histogram retains the cover's dominant hue rather than
 * averaging complementary colors to grey. Only the small palette is cached. */
static lv_color_t cover_color(const char * path)
{
    lv_color_t result=lv_color_hex(BG);
    lv_image_header_t header;
    if(lv_image_decoder_get_info(path,&header)!=LV_RESULT_OK || header.w>2048 || header.h>2048) return result;
    lv_image_decoder_dsc_t dsc;
    lv_image_decoder_args_t args={0};
    if(lv_image_decoder_open(&dsc,path,&args)!=LV_RESULT_OK) return result;
    const lv_draw_buf_t * buf=dsc.decoded;
    if(buf && buf->header.cf==LV_COLOR_FORMAT_ARGB8888 && buf->header.w && buf->header.h) {
        struct { unsigned weight, r, g, b; } bins[512]={{0}};
        unsigned winner=0;
        unsigned step_x=buf->header.w/32+1,step_y=buf->header.h/32+1;
        for(unsigned y=step_y/2;y<buf->header.h;y+=step_y) {
            const lv_color32_t * row=(const lv_color32_t *)(buf->data+y*buf->header.stride);
            for(unsigned x=step_x/2;x<buf->header.w;x+=step_x) {
                unsigned r=row[x].red,g=row[x].green,b=row[x].blue;
                unsigned hi=LV_MAX(r,LV_MAX(g,b)),lo=LV_MIN(r,LV_MIN(g,b));
                unsigned light=(r*54+g*183+b*19)/256;
                if(row[x].alpha<192 || light<24 || light>235) continue;
                unsigned weight=1+(hi-lo)/32;
                unsigned bin=((r>>5)<<6)|((g>>5)<<3)|(b>>5);
                bins[bin].weight+=weight;
                bins[bin].r+=r*weight; bins[bin].g+=g*weight; bins[bin].b+=b*weight;
                if(bins[bin].weight>bins[winner].weight) winner=bin;
            }
        }
        if(bins[winner].weight) {
            unsigned n=bins[winner].weight,r=bins[winner].r/n,g=bins[winner].g/n,b=bins[winner].b/n;
            unsigned grey=(LV_MAX(r,LV_MAX(g,b))+LV_MIN(r,LV_MIN(g,b)))/2;
            r=(r*3+grey*2)/5; g=(g*3+grey*2)/5; b=(b*3+grey*2)/5;
            unsigned peak=LV_MAX(r,LV_MAX(g,b));
            if(peak) result=lv_color_make(10+r*48/peak,10+g*48/peak,10+b*48/peak);
        }
    }
    lv_image_decoder_close(&dsc);
    return result;
}
static lv_color_t queue_background(void) { return lv_color_mix(lv_color_white(),background,12); }
static void apply_background(lv_color_t color)
{
    background=color;
    lv_obj_set_style_bg_color(root,color,0);
    render_background(color);
    lv_color_t sheet=queue_background();
    lv_obj_set_style_bg_color(list_panel,sheet,0);
    lv_obj_set_style_bg_color(queue,sheet,0);
    hachimi_music_state_t s; hachimi_music_state(&s);
    for(unsigned i=0;i<lv_obj_get_child_count(queue);i++)
        lv_obj_set_style_bg_color(lv_obj_get_child(queue,i),i==s.index ? lv_color_mix(lv_color_white(),color,30) : sheet,0);
    lv_obj_set_style_bg_color(volume_panel,lv_color_mix(lv_color_white(),color,24),0);
    lv_obj_set_style_bg_color(lyrics_button,lv_color_mix(lv_color_white(),color,30),0);
    lv_obj_set_style_bg_color(list_button,lv_color_mix(lv_color_white(),color,30),0);
    lv_obj_set_style_bg_color(seek,lv_color_mix(lv_color_white(),color,28),LV_PART_MAIN);
}
static void theme_frame(lv_timer_t * timer)
{
    (void)timer;
    if(!root || !theme_active || !lv_obj_is_visible(root)) return;
    uint32_t elapsed=lv_tick_elaps(theme_started);
    if(elapsed>=360) { theme_active=false; apply_background(theme_target); return; }
    unsigned t=elapsed*255/360;
    unsigned eased=t*t*(765-2*t)/(255*255);
    apply_background(lv_color_mix(theme_target,theme_from,(lv_opa_t)eased));
}
static void change_background(lv_color_t color,bool animate)
{
    theme_active=false;
    theme_target=color;
    if(!animate || !lv_obj_is_visible(root)) { apply_background(color); return; }
    theme_from=background;
    theme_started=lv_tick_get(); theme_active=true;
}

static void register_player_object(lv_obj_t * obj, int x)
{
    if(player_group_count>=sizeof(player_group)/sizeof(player_group[0])) return;
    player_group[player_group_count]=obj;
    player_group_x[player_group_count++]=x;
}

static void apply_player_offset(int offset)
{
    player_offset=offset;
    for(unsigned i=0;i<player_group_count;i++) {
        lv_obj_t * obj=player_group[i];
        if(obj) lv_obj_set_x(obj,player_group_x[i]+offset);
    }
}

static void move_player_group(bool centered)
{
    int target=centered ? PLAYER_CENTER_OFFSET : 0;
    if(target==player_offset_target && (player_offset_active || target==player_offset)) return;
    player_offset_from=player_offset;
    player_offset_target=target;
    player_offset_started=lv_tick_get();
    player_offset_active=player_offset_from!=player_offset_target;
    if(!player_offset_active) apply_player_offset(target);
}

static void layout_frame(lv_timer_t * timer)
{
    (void)timer;
    if(!root || !player_offset_active || !lv_obj_is_visible(root)) return;
    uint32_t elapsed=lv_tick_elaps(player_offset_started);
    if(elapsed>=PLAYER_LAYOUT_DURATION) {
        player_offset_active=false;
        apply_player_offset(player_offset_target);
        return;
    }
    uint32_t t=elapsed*1024/PLAYER_LAYOUT_DURATION;
    uint32_t eased=t*t*(3072-2*t)/(1024*1024);
    int offset=player_offset_from+(int)(((int64_t)(player_offset_target-player_offset_from)*eased)/1024);
    apply_player_offset(offset);
}

typedef enum { ICON_VOLUME, ICON_MODE, ICON_PREV, ICON_PLAY, ICON_NEXT, ICON_LYRICS, ICON_LIST, ICON_CLOSE } icon_t;
static lv_obj_t * text(lv_obj_t * parent,const char * value,int x,int y,int w,int h,unsigned size,uint32_t color)
{
    lv_obj_t * o=lv_label_create(parent); lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h);
    lv_obj_set_style_text_font(o,hachimi_ui_font(size),0);
    lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_label_set_text(o,value); return o;
}
static lv_obj_t * panel(lv_obj_t * parent,int x,int y,int w,int h,uint32_t color)
{
    lv_obj_t * o=lv_obj_create(parent); lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h); lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0); lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    return o;
}
static void line(lv_layer_t * layer,int x1,int y1,int x2,int y2,int width,lv_color_t color)
{
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d); d.color=color; d.width=width;
    d.p1.x=x1; d.p1.y=y1; d.p2.x=x2; d.p2.y=y2; d.round_start=1; d.round_end=1;
    lv_draw_line(layer,&d);
}
static void triangle(lv_layer_t * layer,int x,int y,int direction,lv_color_t color)
{
    lv_draw_triangle_dsc_t d; lv_draw_triangle_dsc_init(&d); d.color=color;
    d.p[0].x=x-5*direction; d.p[0].y=y-6;
    d.p[1].x=x-5*direction; d.p[1].y=y+6;
    d.p[2].x=x+5*direction; d.p[2].y=y;
    lv_draw_triangle(layer,&d);
}
static void icon_draw(lv_event_t * e)
{
    lv_obj_t * o=lv_event_get_target(e); lv_area_t a; lv_obj_get_coords(o,&a);
    int x=(a.x1+a.x2)/2,y=(a.y1+a.y2)/2;
    lv_color_t c=lv_color_hex(WHITE); lv_layer_t * layer=lv_event_get_layer(e);
    icon_t icon=(icon_t)(uintptr_t)lv_event_get_user_data(e);
    if(icon==ICON_PLAY) {
        if(last_playing) { line(layer,x-4,y-7,x-4,y+7,4,c); line(layer,x+4,y-7,x+4,y+7,4,c); }
        else triangle(layer,x+1,y,1,c);
    }
    else if(icon==ICON_PREV || icon==ICON_NEXT) {
        int dir=icon==ICON_NEXT ? 1 : -1;
        triangle(layer,x-4*dir,y,dir,c); triangle(layer,x+5*dir,y,dir,c);
    }
    else if(icon==ICON_LIST) {
        for(int i=-1;i<=1;i++) { line(layer,x-9,y+i*6,x-9,y+i*6,2,c); line(layer,x-3,y+i*6,x+9,y+i*6,2,c); }
    }
    else if(icon==ICON_VOLUME) {
        line(layer,x-9,y-3,x-9,y+3,2,c); line(layer,x-9,y-3,x-5,y-3,2,c); line(layer,x-9,y+3,x-5,y+3,2,c);
        line(layer,x-5,y-3,x,y-7,2,c); line(layer,x,y-7,x,y+7,2,c); line(layer,x,y+7,x-5,y+3,2,c);
        line(layer,x+4,y-4,x+6,y,2,c); line(layer,x+6,y,x+4,y+4,2,c);
        line(layer,x+8,y-7,x+11,y,2,c); line(layer,x+11,y,x+8,y+7,2,c);
    }
    else if(icon==ICON_MODE) {
        for(int i=-1;i<=1;i++) line(layer,x+i*5,y,x+i*5,y,2,c);
    }
    else if(icon==ICON_CLOSE) { line(layer,x-5,y-5,x+5,y+5,2,c); line(layer,x-5,y+5,x+5,y-5,2,c); }
    else {
        line(layer,x-8,y-7,x+8,y-7,2,c); line(layer,x-8,y-7,x-8,y+5,2,c);
        line(layer,x+8,y-7,x+8,y+5,2,c); line(layer,x-8,y+5,x+8,y+5,2,c);
        line(layer,x-3,y+5,x-3,y+9,2,c); line(layer,x-3,y+9,x+2,y+5,2,c);
        line(layer,x-4,y-2,x-2,y-2,2,c); line(layer,x+2,y-2,x+4,y-2,2,c);
    }
}
static void set_panels(void)
{
    if(show_list) { lv_obj_remove_flag(list_panel,LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(lyrics_panel,LV_OBJ_FLAG_HIDDEN); }
    else { lv_obj_add_flag(list_panel,LV_OBJ_FLAG_HIDDEN);
        if(show_lyrics) lv_obj_remove_flag(lyrics_panel,LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(lyrics_panel,LV_OBJ_FLAG_HIDDEN); }
    lv_obj_set_style_bg_opa(list_button,show_list ? LV_OPA_COVER : LV_OPA_TRANSP,0);
    lv_obj_set_style_bg_opa(lyrics_button,show_lyrics && !show_list ? LV_OPA_COVER : LV_OPA_TRANSP,0);
    bool lyrics_visible=show_lyrics && !show_list;
    if(lyrics_visible) {
        lv_obj_remove_flag(mode_text,LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(error_label,LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(mode_text,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(error_label,LV_OBJ_FLAG_HIDDEN);
    }
    move_player_group(!show_lyrics && !show_list);
}
static void icon_click(lv_event_t * e)
{
    icon_t icon=(icon_t)(uintptr_t)lv_event_get_user_data(e);
    hachimi_music_state_t s; hachimi_music_state(&s);
    switch(icon) {
    case ICON_PLAY: hachimi_music_set_playing(!s.playing); break;
    case ICON_PREV: hachimi_music_next(false); break;
    case ICON_NEXT: hachimi_music_next(true); break;
    case ICON_LIST: show_list=!show_list; set_panels(); break;
    case ICON_CLOSE: show_list=false; set_panels(); break;
    case ICON_LYRICS: if(show_list) { show_list=false; show_lyrics=true; } else show_lyrics=!show_lyrics; set_panels(); break;
    case ICON_VOLUME:
        if(lv_obj_has_flag(volume_panel,LV_OBJ_FLAG_HIDDEN)) lv_obj_remove_flag(volume_panel,LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(volume_panel,LV_OBJ_FLAG_HIDDEN);
        break;
    case ICON_MODE: hachimi_music_mode((s.mode+1)%3); break;
    }
}
static lv_obj_t * icon_button(lv_obj_t * parent,icon_t icon,int x,int y)
{
    lv_obj_t * o=lv_button_create(parent); lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y); lv_obj_set_size(o,40,34); lv_obj_set_style_radius(o,5,0);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x353535),0);
    lv_obj_set_style_bg_opa(o,LV_OPA_TRANSP,0);
    lv_obj_set_style_bg_opa(o,LV_OPA_COVER,LV_STATE_PRESSED);
    lv_obj_add_event_cb(o,icon_draw,LV_EVENT_DRAW_MAIN,(void *)(uintptr_t)icon);
    lv_obj_add_event_cb(o,icon_click,LV_EVENT_CLICKED,(void *)(uintptr_t)icon);
    return o;
}
static void row_click(lv_event_t * e) { hachimi_music_select((uintptr_t)lv_event_get_user_data(e),true); }
static void rebuild_queue(void)
{
    lv_obj_clean(queue); hachimi_music_state_t s; hachimi_music_state(&s);
    lv_label_set_text_fmt(queue_title,"播放列表 · %u 首",(unsigned)s.count);
    for(size_t i=0;i<s.count;i++) {
        const hachimi_music_track_t * t=hachimi_music_track(i);
        lv_obj_t * row=panel(queue,0,i*58,308,57, i==s.index ? 0x373d3a : 0x282828);
        lv_obj_add_flag(row,LV_OBJ_FLAG_CLICKABLE); lv_obj_set_style_radius(row,6,0);
        lv_obj_add_event_cb(row,row_click,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
        char number[16]; snprintf(number,sizeof(number),"%02u",(unsigned)i+1);
        text(row,number,10,17,27,20,12,i==s.index ? ACCENT : MUTED);
        lv_obj_t * name=text(row,t->title,43,7,216,24,16,i==s.index ? 0xdde8e3 : WHITE);
        lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);
        text(row,t->artist,43,32,210,19,12,MUTED);
        char time[16]; snprintf(time,sizeof(time),"%u:%02u",t->duration_ms/60000,t->duration_ms/1000%60);
        text(row,time,265,19,43,20,12,MUTED);
    }
}
static void seek_released(lv_event_t * e) { hachimi_music_seek(lv_slider_get_value(lv_event_get_target(e))); }
static void volume_changed(lv_event_t * e) { hachimi_music_volume(lv_slider_get_value(lv_event_get_target(e))); }
static void load_track(const hachimi_music_state_t * s)
{
    bool animate=loaded_track!=(size_t)-1;
    loaded_track=s->index; lyric_line=-2;
    const hachimi_music_track_t * t=hachimi_music_track(s->index);
    lv_label_set_text(title,t ? t->title : "暂无音乐");
    char info[512]; const char * album=t ? strstr(t->album," - ") : NULL;
    snprintf(info,sizeof(info),"%s%s%s",t ? t->artist : "",t ? " — " : "",t ? (album ? album+3 : t->album) : "");
    lv_label_set_text(subtitle,info);
    bool have_cover=false;
    if(t) {
        char file[1024]; snprintf(file,sizeof(file),"%s",t->path);
        char * ext=strrchr(file,'.'); if(ext) strcpy(ext,".png");
        FILE * f=fopen(file,"rb"); if(f) { fclose(f); have_cover=true; snprintf(cover_path,sizeof(cover_path),"A:%s",file); lv_image_set_src(cover,cover_path); }
        if(lrc) { if(ext) strcpy(ext,".lrc"); hachimi_music_read_lrc(file,lrc); }
    }
    if(!t && lrc) memset(lrc,0,sizeof(*lrc));
    if(have_cover) { lv_obj_remove_flag(cover,LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(cover_placeholder,LV_OBJ_FLAG_HIDDEN); }
    else { lv_obj_add_flag(cover,LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(cover_placeholder,LV_OBJ_FLAG_HIDDEN); }
    rebuild_queue();
    lv_color_t color=lv_color_hex(BG);
    if(have_cover) {
        if(s->index<HACHIMI_MUSIC_MAX && theme_cached[s->index]) color=lv_color_hex(theme_colors[s->index]);
        else {
            color=cover_color(cover_path);
            if(s->index<HACHIMI_MUSIC_MAX) { theme_colors[s->index]=lv_color_to_u32(color)&0xffffff; theme_cached[s->index]=true; }
        }
    }
    change_background(color,animate);
}
static void update(lv_timer_t * timer)
{
    (void)timer; if(!root || !lv_obj_is_visible(root)) return;
    hachimi_music_state_t s; hachimi_music_state(&s);
    if(s.index!=loaded_track) load_track(&s);
    if(last_playing!=s.playing) { last_playing=s.playing; lv_obj_invalidate(play_button); }
    if(!lv_slider_is_dragged(seek)) {
        lv_slider_set_range(seek,0,s.duration_ms ? s.duration_ms : 1);
        lv_slider_set_value(seek,s.position_ms,LV_ANIM_OFF);
    }
    lv_label_set_text_fmt(elapsed,"%u:%02u",s.position_ms/60000,s.position_ms/1000%60);
    uint32_t rest=s.duration_ms>s.position_ms ? s.duration_ms-s.position_ms : 0;
    lv_label_set_text_fmt(remaining,"-%u:%02u",rest/60000,rest/1000%60);
    const char * modes[]={"列表循环","单曲循环","随机播放"};
    lv_label_set_text(mode_text,modes[s.mode]);
    lv_label_set_text(error_label,s.error);
    lv_label_set_text_fmt(volume_text,"音量 %u%%",s.volume);
    int line=lrc ? hachimi_music_lyric_at(lrc,s.position_ms) : -1;
    if(line!=lyric_line) {
        lyric_line=line;
        for(int i=0;i<5;i++) {
            int at=line+i-2; const char * value="";
            if(lrc && at>=0 && (size_t)at<lrc->count) value=lrc->lines[at].text;
            if(i==2 && (!lrc || !lrc->count)) value="暂无歌词";
            else if(i==2 && line<0) value="前奏";
            lv_label_set_text(lyric_labels[i],value);
        }
    }
}
static void view_delete(lv_event_t * e)
{
    theme_active=false;
    if(theme_timer) lv_timer_delete(theme_timer);
    theme_timer=NULL;
    if(layout_timer) lv_timer_delete(layout_timer);
    layout_timer=NULL;
    (void)e; if(view_timer) lv_timer_delete(view_timer); view_timer=NULL;
    root=NULL; player_group_count=0; player_offset=player_offset_from=player_offset_target=0; player_offset_active=false;
    free(lrc); lrc=NULL; loaded_track=(size_t)-1; lyric_line=-2;
}
static void poll(lv_timer_t * t) { (void)t; hachimi_music_poll(); }
static void start(void) { hachimi_music_init(); if(!system_timer) system_timer=lv_timer_create(poll,150,NULL); }
static void stop(void) { if(system_timer) lv_timer_delete(system_timer); system_timer=NULL; hachimi_music_shutdown(); }
static lv_obj_t * create(lv_obj_t * parent)
{
    start(); last_playing=false; show_list=false; show_lyrics=true;
    player_group_count=0; player_offset=player_offset_from=player_offset_target=0; player_offset_active=false;
    root=panel(parent,0,0,800,480,BG); lv_obj_add_event_cb(root,view_delete,LV_EVENT_DELETE,NULL);
    background=lv_color_hex(BG); theme_target=background;
    memset(theme_cached,0,sizeof(theme_cached));
    background_image=lv_image_create(root); lv_obj_set_pos(background_image,0,0);
    lv_image_set_src(background_image,&background_dsc);
    lv_obj_remove_flag(background_image,LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t * sleeve=panel(root,84,58,264,264,0x303534); register_player_object(sleeve,84); lv_obj_set_style_radius(sleeve,6,0);
    lv_obj_set_style_clip_corner(sleeve,true,0);
    cover_placeholder=panel(sleeve,0,0,264,264,0x303534);
    lv_obj_set_style_bg_grad_color(cover_placeholder,lv_color_hex(0x202322),0);
    lv_obj_set_style_bg_grad_dir(cover_placeholder,LV_GRAD_DIR_VER,0);
    lv_obj_t * placeholder=text(cover_placeholder,"音乐",0,111,264,40,28,0x8a9893);
    lv_obj_set_style_text_align(placeholder,LV_TEXT_ALIGN_CENTER,0);
    cover=lv_image_create(sleeve); lv_obj_set_size(cover,264,264); lv_image_set_inner_align(cover,LV_IMAGE_ALIGN_STRETCH);
    title=text(root,"",36,331,360,27,18,WHITE); register_player_object(title,36); lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
    lv_label_set_long_mode(title,LV_LABEL_LONG_SCROLL_CIRCULAR);
    subtitle=text(root,"",36,359,360,22,14,MUTED); register_player_object(subtitle,36); lv_obj_set_style_text_align(subtitle,LV_TEXT_ALIGN_CENTER,0);
    lv_label_set_long_mode(subtitle,LV_LABEL_LONG_DOT);
    elapsed=text(root,"0:00",42,386,45,19,12,MUTED); register_player_object(elapsed,42);
    remaining=text(root,"-0:00",344,386,51,19,12,MUTED); register_player_object(remaining,344);
    lv_obj_set_style_text_align(remaining,LV_TEXT_ALIGN_RIGHT,0);
    seek=lv_slider_create(root); lv_obj_set_pos(seek,94,395); register_player_object(seek,94); lv_obj_set_size(seek,238,4);
    lv_obj_set_style_bg_color(seek,lv_color_hex(0x414141),LV_PART_MAIN);
    lv_obj_set_style_bg_color(seek,lv_color_hex(0xa6aaa8),LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(seek,lv_color_hex(0xd6d6d6),LV_PART_KNOB);
    lv_obj_set_style_pad_all(seek,2,LV_PART_KNOB); lv_obj_set_style_outline_width(seek,0,0);
    lv_obj_add_event_cb(seek,seek_released,LV_EVENT_RELEASED,NULL);
    lv_obj_t * volume_button=icon_button(root,ICON_VOLUME,36,410); register_player_object(volume_button,36);
    lv_obj_t * mode_button=icon_button(root,ICON_MODE,88,410); register_player_object(mode_button,88);
    lv_obj_t * prev_button=icon_button(root,ICON_PREV,140,410); register_player_object(prev_button,140);
    play_button=icon_button(root,ICON_PLAY,192,410); register_player_object(play_button,192);
    lv_obj_t * next_button=icon_button(root,ICON_NEXT,244,410); register_player_object(next_button,244);
    lyrics_button=icon_button(root,ICON_LYRICS,296,410); register_player_object(lyrics_button,296);
    list_button=icon_button(root,ICON_LIST,348,410); register_player_object(list_button,348);
    lyrics_panel=panel(root,432,64,336,360,BG);
    lv_obj_set_style_bg_opa(lyrics_panel,LV_OPA_TRANSP,0);
    for(int i=0;i<5;i++) {
        lyric_labels[i]=text(lyrics_panel,"",4,12+i*68,326,62,i==2?22:18,i==2?0xe1e1e1:(i==1||i==3?0x7c7c7c:0x494949));
        lv_obj_set_style_text_align(lyric_labels[i],LV_TEXT_ALIGN_LEFT,0);
        lv_label_set_long_mode(lyric_labels[i],LV_LABEL_LONG_WRAP);
    }
    mode_text=text(root,"列表循环",442,434,280,20,12,0x626262);
    error_label=text(root,"",434,457,344,20,12,0xb69b8a); lv_label_set_long_mode(error_label,LV_LABEL_LONG_DOT);
    list_panel=panel(root,430,58,352,370,0x282828); lv_obj_set_style_radius(list_panel,10,0);
    queue_title=text(list_panel,"播放列表",16,14,266,28,18,WHITE); icon_button(list_panel,ICON_CLOSE,303,9);
    queue=panel(list_panel,10,56,332,302,0x282828); lv_obj_add_flag(queue,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(queue,LV_DIR_VER); lv_obj_set_style_pad_right(queue,10,0);
    lv_obj_set_style_bg_color(queue,lv_color_hex(0x656565),LV_PART_SCROLLBAR);
    volume_panel=panel(root,36,325,350,65,0x353535); lv_obj_set_style_radius(volume_panel,8,0);
    volume_text=text(volume_panel,"音量",14,9,100,22,14,WHITE);
    lv_obj_t * volume=lv_slider_create(volume_panel); lv_obj_set_pos(volume,118,29); lv_obj_set_size(volume,209,4);
    lv_slider_set_range(volume,0,100); hachimi_music_state_t s; hachimi_music_state(&s); lv_slider_set_value(volume,s.volume,LV_ANIM_OFF);
    lv_obj_set_style_bg_color(volume,lv_color_hex(0x606060),LV_PART_MAIN);
    lv_obj_set_style_bg_color(volume,lv_color_hex(0xc1c7c4),LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volume,lv_color_hex(0xdddddd),LV_PART_KNOB); lv_obj_set_style_pad_all(volume,4,LV_PART_KNOB);
    lv_obj_add_event_cb(volume,volume_changed,LV_EVENT_VALUE_CHANGED,NULL); lv_obj_add_flag(volume_panel,LV_OBJ_FLAG_HIDDEN);
    ios_status_bar_create(root,lv_color_hex(0xbdbdbd));
    lrc=calloc(1,sizeof(*lrc)); loaded_track=(size_t)-1;
    theme_timer=lv_timer_create(theme_frame,30,NULL);
    layout_timer=lv_timer_create(layout_frame,20,NULL);
    view_timer=lv_timer_create(update,100,NULL); set_panels(); update(NULL); return root;
}
static void on_open(lv_obj_t * view)
{
    (void)view;
    if(view_timer) lv_timer_resume(view_timer);
    if(theme_timer) lv_timer_resume(theme_timer);
    if(layout_timer) { lv_timer_resume(layout_timer); player_offset_active=false; apply_player_offset(player_offset_target); }
    update(NULL);
    change_background(theme_target,false);
}
static void on_close(lv_obj_t * view)
{
    (void)view;
    if(view_timer) lv_timer_pause(view_timer);
    theme_active=false;
    if(theme_timer) lv_timer_pause(theme_timer);
    player_offset_active=false;
    if(layout_timer) lv_timer_pause(layout_timer);
    if(volume_panel) lv_obj_add_flag(volume_panel,LV_OBJ_FLAG_HIDDEN);
}
const hachimi_app_module_t hachimi_music_app={"music",create,on_open,on_close,start,stop};
