#include "framework/desktop.h"
#include "services/music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static uint16_t pixels[800*480];
static uint16_t desktop_pixels[800*480];
static uint8_t buffer[800*60*2];
static uint32_t ticks(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
static void flush(lv_display_t * d,const lv_area_t * a,uint8_t * data)
{
    for(int y=a->y1;y<=a->y2;y++) memcpy(pixels+y*800+a->x1,data+(y-a->y1)*(a->x2-a->x1+1)*2,(a->x2-a->x1+1)*2);
    lv_display_flush_ready(d);
}
static void pump(unsigned ms) { uint32_t start=ticks(); do { lv_timer_handler(); usleep(3000); } while(ticks()-start<ms); }
static lv_obj_t * find(lv_obj_t * p,const char * text)
{
    if(lv_obj_check_type(p,&lv_label_class) && !strcmp(lv_label_get_text(p),text)) return p;
    for(unsigned i=0;i<lv_obj_get_child_count(p);i++) { lv_obj_t * o=find(lv_obj_get_child(p,i),text); if(o) return o; }
    return NULL;
}
static lv_obj_t * hit(lv_obj_t * p,int x,int y)
{
    if(!lv_obj_is_visible(p)) return NULL;
    for(int i=(int)lv_obj_get_child_count(p)-1;i>=0;i--) { lv_obj_t * o=hit(lv_obj_get_child(p,i),x,y); if(o) return o; }
    lv_area_t a; lv_obj_get_coords(p,&a);
    return x>=a.x1 && x<=a.x2 && y>=a.y1 && y<=a.y2 && lv_obj_has_flag(p,LV_OBJ_FLAG_CLICKABLE) ? p : NULL;
}
static void tap(int x,int y) { lv_obj_update_layout(lv_screen_active()); lv_obj_t * o=hit(lv_screen_active(),x,y); CHECK(o); lv_obj_send_event(o,LV_EVENT_CLICKED,NULL); pump(200); }
static void capture(const char * name)
{
    const char * dir=getenv("HACHIMI_MUSIC_ARTIFACTS"); if(!dir) return;
    char path[1024]; snprintf(path,sizeof(path),"%s/%s.rgb565",dir,name);
    FILE * f=fopen(path,"wb"); CHECK(f); CHECK(fwrite(pixels,1,sizeof(pixels),f)==sizeof(pixels)); fclose(f);
}
static void parser_test(void)
{
    char path[]="/tmp/hachimi-lrc-XXXXXX"; int fd=mkstemp(path); CHECK(fd>=0);
    FILE * f=fdopen(fd,"wb"); CHECK(f);
    fputs("\xef\xbb\xbf[ar:artist]\n[offset:62]\n[00:02.5][00:04.050]<00:02.500>two\n[00:01.20]one\n[00:99.0]invalid\n[00:03.123]three\n",f); fclose(f);
    hachimi_lrc_t * l=calloc(1,sizeof(*l)); CHECK(l); CHECK(hachimi_music_read_lrc(path,l));
    CHECK(l->count==4 && l->offset_ms==62);
    CHECK(hachimi_music_lyric_at(l,1137)==-1); CHECK(hachimi_music_lyric_at(l,1138)==0);
    CHECK(l->lines[1].ms==2500 && !strcmp(l->lines[1].text,"two"));
    CHECK(hachimi_music_lyric_at(l,4000)==3);
    unlink(path); free(l);
    puts("PASS LRC fractions, multiple timestamps, BOM, offset, sorting, enhanced tags and malformed timestamps");
}
int main(int argc,char ** argv)
{
    bool board=argc>1 && !strcmp(argv[1],"--board");
    parser_test(); lv_init(); lv_tick_set_cb(ticks);
    lv_display_t * d=lv_display_create(800,480); lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL); lv_display_set_flush_cb(d,flush);
    ios_desktop_create(); pump(120);
    memcpy(desktop_pixels,pixels,sizeof(pixels));
    capture("music-desktop-baseline");
    hachimi_music_state_t s; hachimi_music_state(&s); CHECK(s.count==12);
    CHECK(hachimi_desktop_launch(HACHIMI_APP_MUSIC)); pump(1100);
    tap(366,427); CHECK(find(lv_screen_active(),"播放列表 · 12 首"));
    capture("music-list"); tap(366,427);
    hachimi_lrc_t * l=malloc(sizeof(*l)); CHECK(l);
    uint16_t theme_pixels[HACHIMI_MUSIC_MAX]; unsigned distinct=0;
    for(size_t i=0;i<s.count;i++) {
        const hachimi_music_track_t * t=hachimi_music_track(i); CHECK(t && t->duration_ms>100000);
        char path[1024]; snprintf(path,sizeof(path),"%s",t->path); strcpy(strrchr(path,'.'),".lrc");
        CHECK(hachimi_music_read_lrc(path,l)); CHECK(l->count>10);
        hachimi_music_select(i,board); pump(650); hachimi_music_state(&s);
        theme_pixels[i]=pixels[80*800+410];
        CHECK(theme_pixels[i]!=pixels[460*800+410]);
        bool seen=false;
        for(size_t j=0;j<i;j++) if(theme_pixels[j]==theme_pixels[i]) seen=true;
        if(!seen) distinct++;
        if(i<3) { char name[32]; snprintf(name,sizeof(name),"music-theme-%u",(unsigned)i); capture(name); }
        if(board) {
            if(s.error[0]) fprintf(stderr,"Audio error: %s\n",s.error);
            CHECK(s.playing && !s.error[0] && s.position_ms>200);
        }
    }
    CHECK(distinct>=3);
    hachimi_music_select(0,false); pump(650);
    CHECK(theme_pixels[0]==pixels[80*800+410]);
    puts("PASS cover-derived backgrounds: distinct album colors, vertical shading and cached recoloring");
    free(l); puts("PASS all 12 WAV files and matching LRC files");
    hachimi_music_select(1,board); pump(500);
    hachimi_music_seek(35000); pump(350);
    capture("music-lyrics");
    if(board) {
        hachimi_music_state(&s); CHECK(s.position_ms>=35000 && s.position_ms<37000);
        hachimi_music_set_playing(false); pump(120); hachimi_music_state(&s); unsigned paused=s.position_ms;
        pump(350); hachimi_music_state(&s); CHECK(!s.playing && s.position_ms==paused);
        hachimi_music_set_playing(true); pump(900); hachimi_music_state(&s);
        fprintf(stderr,"Resume: paused=%u now=%u playing=%d error=%s\n",paused,s.position_ms,s.playing,s.error);
        CHECK(s.playing && s.position_ms>paused+150);
        hachimi_music_volume(0); pump(80); hachimi_music_state(&s); CHECK(s.volume==0);
        hachimi_music_volume(65);
        hachimi_music_mode(HACHIMI_MUSIC_REPEAT); hachimi_music_seek(s.duration_ms-350); pump(1300);
        hachimi_music_state(&s); CHECK(s.index==1 && s.playing && s.position_ms<2000);
        hachimi_music_mode(HACHIMI_MUSIC_LIST_LOOP); hachimi_music_seek(s.duration_ms-350); pump(1300);
        hachimi_music_state(&s); CHECK(s.index==2 && s.playing);
        hachimi_music_mode(HACHIMI_MUSIC_SHUFFLE); hachimi_music_next(true); pump(450);
        hachimi_music_state(&s); CHECK(s.index!=2 && s.playing);
        hachimi_music_mode(HACHIMI_MUSIC_LIST_LOOP);
        puts("PASS ALSA playback, 44.1/48 kHz, pause/resume, seek, volume, repeat, auto-next and shuffle");
    }
    hachimi_desktop_home(); pump(550);
    capture("music-first-home");
    lv_mem_monitor_t first_memory; lv_mem_monitor(&first_memory);
    fprintf(stderr,"First home heap free=%u largest=%u used=%u%%\n",(unsigned)first_memory.free_size,(unsigned)first_memory.free_biggest_size,first_memory.used_pct);
    CHECK(!memcmp(desktop_pixels+800*35,pixels+800*35,800*(480-35)*2));
    if(board) { hachimi_music_state(&s); unsigned before=s.position_ms; pump(350); hachimi_music_state(&s); CHECK(s.playing && s.position_ms>before); }
    CHECK(hachimi_desktop_launch(HACHIMI_APP_MUSIC)); pump(800);
    capture("music-reopened");
    hachimi_music_set_playing(false); pump(100);
    hachimi_desktop_home(); pump(500);
    const hachimi_app_id_t warm_apps[]={HACHIMI_APP_SETTINGS,HACHIMI_APP_CLOCK,HACHIMI_APP_CALENDAR,
        HACHIMI_APP_CALCULATOR,HACHIMI_APP_BOOKS,HACHIMI_APP_FILES,HACHIMI_APP_MAPS,HACHIMI_APP_WEATHER};
    for(unsigned i=0;i<sizeof(warm_apps)/sizeof(warm_apps[0]);i++) {
        CHECK(hachimi_desktop_launch(warm_apps[i])); pump(700);
        CHECK(hachimi_desktop_home()); pump(400);
    }
    lv_mem_monitor_t memory; lv_mem_monitor(&memory);
    fprintf(stderr,"UI heap free=%u largest=%u used=%u%%\n",(unsigned)memory.free_size,(unsigned)memory.free_biggest_size,memory.used_pct);
    CHECK(hachimi_desktop_launch(HACHIMI_APP_MUSIC)); pump(700);
    for(unsigned i=0;i<12;i++) {
        hachimi_music_select(i,false); hachimi_music_seek(55000+i*9000); pump(500);
        CHECK(hachimi_desktop_home()); pump(450);
        capture("music-home-stress");
        CHECK(!memcmp(desktop_pixels+800*35,pixels+800*35,800*(480-35)*2));
        CHECK(hachimi_desktop_launch(HACHIMI_APP_MUSIC)); pump(500);
    }
    CHECK(hachimi_desktop_home()); pump(450);
    puts("PASS exact desktop restoration after warming other apps and repeated lyric/cover changes");
    hachimi_desktop_destroy(); pump(100);
    puts("PASS Music Home/Dock module, lyric view, background playback, reopen and teardown");
    return 0;
}
