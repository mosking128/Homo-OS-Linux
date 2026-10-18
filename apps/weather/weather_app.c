#include "weather_app.h"
#include "framework/ui/fonts.h"
#include "services/weather.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#define WHITE 0xf4f8ff
#define PALE 0xaac5df
#define CARD 0x244b70
#define GOLD 0xffd375
static lv_obj_t * root,* content,* overlay,* input,* notice;
static lv_timer_t * timer;
static bool active;
static time_t last_auto;
static const char * const names[]={"穿衣","紫外线","洗车","舒适度","感冒","运动"};
static const char * const city_names[]={"北京","上海","杭州","成都","武汉","西安"};
static const char * const cities[]={"beijing","shanghai","hangzhou","chengdu","wuhan","xian"};
static void render(void);
static lv_obj_t * panel(lv_obj_t * p,int x,int y,int w,int h,int radius,uint32_t color)
{
    lv_obj_t * o=lv_obj_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_radius(o,radius,0);lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;
}
static lv_obj_t * label(lv_obj_t * p,const char * text,int x,int y,int w,int h,unsigned size,uint32_t color)
{
    lv_obj_t * o=lv_label_create(p);lv_label_set_text(o,text);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_text_font(o,hachimi_ui_font(size),0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_label_set_long_mode(o,LV_LABEL_LONG_MODE_DOTS);lv_obj_clear_flag(o,LV_OBJ_FLAG_CLICKABLE);return o;
}
static lv_obj_t * button(lv_obj_t * p,const char * text,int x,int y,int w,int h,lv_event_cb_t cb,intptr_t data)
{
    lv_obj_t * o=panel(p,x,y,w,h,12,CARD);lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x39638b),LV_STATE_PRESSED);
    lv_obj_t * l=label(o,text,0,0,w,h,14,WHITE);lv_obj_set_size(l,LV_SIZE_CONTENT,LV_SIZE_CONTENT);lv_obj_center(l);
    if(cb)lv_obj_add_event_cb(o,cb,LV_EVENT_CLICKED,(void*)data);
    return o;
}
/* Weather symbols use small native shapes; no large wallpaper, GPU effects or
 * continuously invalidated animation on the framebuffer target. */
static void icon(lv_obj_t * p,int code,int x,int y,int size)
{
    lv_obj_t * box=panel(p,x,y,size,size,0,CARD);lv_obj_set_style_bg_opa(box,LV_OPA_TRANSP,0);lv_obj_clear_flag(box,LV_OBJ_FLAG_CLICKABLE);
    int u=size/10;if(u<1)u=1;
    bool clear=code>=0&&code<=3,part=code>=4&&code<=8,rain=code>=10&&code<=19,snow=code>=20&&code<=25;
    if(clear||part){
        panel(box,part?4*u:2*u,u,5*u,5*u,LV_RADIUS_CIRCLE,GOLD);
        if(code==1||code==3||code==5||code==7)panel(box,(part?4:2)*u+2*u,0,5*u,5*u,LV_RADIUS_CIRCLE,0x1b3e61);
    }
    if(!clear){
        uint32_t cloud=rain?0xb7cee1:0xe7f1fc;
        panel(box,2*u,3*u,4*u,4*u,LV_RADIUS_CIRCLE,cloud);
        panel(box,4*u,4*u,4*u,3*u,LV_RADIUS_CIRCLE,cloud);
        panel(box,u,5*u,8*u,2*u,u,cloud);
        if(rain)for(int k=0;k<3;k++)panel(box,(2+2*k)*u,8*u,u,2*u,u,0x75caff);
        else if(snow)for(int k=0;k<3;k++)panel(box,(2+2*k)*u,8*u,u,u,LV_RADIUS_CIRCLE,WHITE);
        else if(code>=26&&code<=31)for(int k=0;k<2;k++)panel(box,u,(8+k)*u,8*u,1,u,PALE);
    }
}
static bool stale(const hachimi_weather_t * d,unsigned section)
{
    time_t now=time(NULL);return !d->fetched[section]||now<d->fetched[section]||now-d->fetched[section]>3600;
}
static void close_overlay(lv_event_t * e)
{
    (void)e;if(overlay){lv_obj_delete(overlay);overlay=NULL;input=NULL;}
}
static void refresh(lv_event_t * e)
{
    (void)e;bool started=hachimi_weather_refresh();render();
    if(!started&&!hachimi_weather_busy()&&!hachimi_weather_data()->errors[0][0])lv_label_set_text(notice,"已是最新请求，请稍后再刷新");
}
static void choose(lv_event_t * e)
{
    intptr_t n=(intptr_t)lv_event_get_user_data(e);
    const char * value=n<0?lv_textarea_get_text(input):cities[n];
    char selected[128];snprintf(selected,sizeof(selected),"%s",value);
    if(hachimi_weather_busy()){lv_label_set_text(notice,"正在更新天气，请稍候再切换城市");return;}
    if(!hachimi_weather_select(selected)){lv_label_set_text(notice,"请输入城市拼音，例如 beijing");return;}
    close_overlay(NULL);hachimi_weather_refresh();render();
}
static void keyboard_event(lv_event_t * e)
{
    if(lv_event_get_code(e)==LV_EVENT_CANCEL)close_overlay(NULL);
    else if(lv_event_get_code(e)==LV_EVENT_READY){
        if(hachimi_weather_busy())return;
        char selected[128];snprintf(selected,sizeof(selected),"%s",lv_textarea_get_text(input));
        if(hachimi_weather_select(selected)){close_overlay(NULL);hachimi_weather_refresh();render();}
    }
}
static void show_cities(lv_event_t * e)
{
    (void)e;if(overlay)return;
    overlay=panel(root,16,40,768,414,20,0x163653);
    label(overlay,"选择城市",24,17,500,30,22,WHITE);button(overlay,"完成",674,14,70,36,close_overlay,0);
    for(int k=0;k<6;k++)button(overlay,city_names[k],24+k*120,58,108,36,choose,k);
    input=lv_textarea_create(overlay);lv_obj_set_pos(input,24,105);lv_obj_set_size(input,600,44);lv_textarea_set_one_line(input,true);
    lv_textarea_set_max_length(input,100);lv_textarea_set_placeholder_text(input,"输入城市拼音，如 beijing");
    lv_obj_set_style_text_font(input,hachimi_ui_font(16),0);lv_obj_set_style_radius(input,10,0);
    button(overlay,"查询",638,105,106,44,choose,-1);
    lv_obj_t * keyboard=lv_keyboard_create(overlay);lv_obj_set_align(keyboard,LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(keyboard,16,163);lv_obj_set_size(keyboard,736,239);
    lv_keyboard_set_textarea(keyboard,input);lv_keyboard_set_mode(keyboard,LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_event_cb(keyboard,keyboard_event,LV_EVENT_READY,NULL);lv_obj_add_event_cb(keyboard,keyboard_event,LV_EVENT_CANCEL,NULL);
}
static void show_tip(lv_event_t * e)
{
    if(overlay)return;
    unsigned k=(unsigned)(uintptr_t)lv_event_get_user_data(e);const hachimi_weather_t * d=hachimi_weather_data();
    overlay=panel(root,122,115,556,255,20,0x163653);
    label(overlay,d->life_names[k][0]?d->life_names[k]:names[k],24,20,420,30,22,PALE);button(overlay,"关闭",452,16,80,38,close_overlay,0);
    label(overlay,d->brief[k][0]?d->brief[k]:"暂无数据",24,67,500,40,28,WHITE);
    lv_obj_t * desc=label(overlay,d->details[k][0]?d->details[k]:"今天的生活指数简报。",24,122,508,95,16,PALE);
    lv_label_set_long_mode(desc,LV_LABEL_LONG_MODE_WRAP);
    label(overlay,"当日生活建议",24,224,500,20,12,PALE);
}
static void render(void)
{
    if(!content)return;
    lv_obj_clean(content);const hachimi_weather_t * d=hachimi_weather_data();
    char b[256];
    label(content,"天气",28,48,88,38,28,WHITE);
    lv_obj_t * cb=button(content,"切换城市",132,48,100,36,show_cities,0);
    if(hachimi_weather_busy())lv_obj_add_state(cb,LV_STATE_DISABLED);
    label(content,"每一程，都有好天气",284,56,300,24,16,PALE);
    lv_obj_t * rb=button(content,hachimi_weather_busy()?"更新中…":"刷新",680,48,92,36,refresh,0);
    if(hachimi_weather_busy())lv_obj_add_state(rb,LV_STATE_DISABLED);
    const char * city=d->city[0]?d->city:hachimi_weather_location();
    for(int k=0;!d->city[0]&&k<6;k++)if(!strcmp(city,cities[k])){city=city_names[k];break;}
    label(content,city,28,111,240,40,32,WHITE);
    label(content,"当前天气",29,157,120,22,14,PALE);
    snprintf(b,sizeof(b),d->now_valid?"%d°":"--°",d->temperature);
    label(content,b,23,180,170,88,64,WHITE);
    if(d->now_valid)icon(content,d->code,173,191,70);
    label(content,d->now_valid?d->text:"等待天气更新",29,270,224,34,22,WHITE);
    if(d->days){
        snprintf(b,sizeof(b),"最高 %d°  ·  最低 %d°",d->daily[0].high,d->daily[0].low);label(content,b,29,312,226,26,16,PALE);
        label(content,"今日风况",29,362,220,20,12,PALE);
        snprintf(b,sizeof(b),"%s%s%s",d->daily[0].wind[0]?d->daily[0].wind:"暂无风况",d->daily[0].scale[0]?" · ":"",d->daily[0].scale);
        if(d->daily[0].scale[0])strncat(b,"级",sizeof(b)-strlen(b)-1);
        label(content,b,29,387,228,23,16,WHITE);
        if(d->daily[0].speed[0]){snprintf(b,sizeof(b),"%s km/h · 预报风速",d->daily[0].speed);label(content,b,29,415,228,20,12,PALE);}
    }else label(content,"联网后查看三日预报与生活建议",29,316,215,80,14,PALE);
    lv_obj_t * forecast=panel(content,280,104,492,204,16,CARD);
    label(forecast,"三日预报",16,13,200,22,16,PALE);
    label(forecast,stale(d,1)?"等待更新":"最低 / 最高",340,16,134,20,12,PALE);
    int min=100,max=-100;for(unsigned k=0;k<d->days;k++){if(d->daily[k].low<min)min=d->daily[k].low;if(d->daily[k].high>max)max=d->daily[k].high;}
    int span=max-min;if(span<1)span=1;
    static const char * const day_names[]={"今天","明天","后天"};
    for(unsigned k=0;k<3;k++){
        int y=47+(int)k*50;
        if(k)panel(forecast,16,y-5,460,1,0,0x365c7c);
        label(forecast,day_names[k],16,y,48,22,16,WHITE);
        if(k<d->days){
            const hachimi_weather_day_t * day=&d->daily[k];
            label(forecast,day->date+5,16,y+23,52,18,12,PALE);icon(forecast,day->code,76,y-3,39);
            label(forecast,day->day,128,y,105,21,16,WHITE);
            snprintf(b,sizeof(b),"夜间 %s",day->night[0]?day->night:"--");label(forecast,b,128,y+23,105,18,12,PALE);
            snprintf(b,sizeof(b),"%d°",day->low);label(forecast,b,251,y+9,44,25,18,PALE);
            panel(forecast,300,y+17,109,7,4,0x183958);
            int left=(day->low-min)*91/span,width=(day->high-day->low)*91/span+18;
            lv_obj_t * bar=panel(forecast,300+left,y+17,width,7,4,0x77c9f5);
            lv_obj_set_style_bg_grad_color(bar,lv_color_hex(GOLD),0);lv_obj_set_style_bg_grad_dir(bar,LV_GRAD_DIR_HOR,0);
            snprintf(b,sizeof(b),"%d°",day->high);label(forecast,b,428,y+9,55,25,18,WHITE);
        }else label(forecast,d->errors[1][0]?"暂时无法获取预报":"暂无预报数据",128,y+8,310,26,14,PALE);
    }
    label(content,"生活建议",284,320,180,24,16,PALE);
    label(content,stale(d,2)?"等待更新":"今日",690,322,82,20,12,PALE);
    int count=0;
    for(int k=0;k<6;k++){
        if(!d->brief[k][0])continue;
        int x=280+(count%3)*167,y=352+(count/3)*44;count++;
        lv_obj_t * tip=button(content,"",x,y,158,38,show_tip,k);
        label(tip,d->life_names[k][0]?d->life_names[k]:names[k],10,10,62,20,12,PALE);
        label(tip,d->brief[k][0]?d->brief[k]:"暂无",75,8,76,24,16,WHITE);
    }
    if(!count)label(content,"生活建议暂不可用，请联网后刷新",284,363,476,45,16,PALE);
    const char * error=NULL;for(unsigned k=0;k<3;k++)if(d->errors[k][0]){error=d->errors[k];break;}
    if(hachimi_weather_busy())snprintf(b,sizeof(b),"正在获取最新天气…");
    else if(error)snprintf(b,sizeof(b),"%s%s",error,d->now_valid?" · 显示上次数据":"");
    else if(d->updated[0])snprintf(b,sizeof(b),"%s%.10s %.5s 更新",stale(d,0)?"上次天气 · ":"",d->updated,strlen(d->updated)>11?d->updated+11:"");
    else snprintf(b,sizeof(b),"点击刷新获取天气");
    notice=label(content,b,29,449,737,20,12,PALE);
}
static void tick(lv_timer_t * t)
{
    (void)t;bool changed=hachimi_weather_poll();if(changed&&active)render();
    time_t now=time(NULL);
    if(active&&!hachimi_weather_busy()&&now-last_auto>=1800){last_auto=now;hachimi_weather_refresh();render();}
}
static void deleted(lv_event_t * e)
{
    (void)e;if(timer)lv_timer_delete(timer);timer=NULL;root=content=overlay=input=notice=NULL;active=false;
}
static lv_obj_t * create(lv_obj_t * parent)
{
    root=panel(parent,0,0,800,480,0,0x193c5a);
    content=panel(root,0,0,800,480,0,0);lv_obj_set_style_bg_opa(content,LV_OPA_TRANSP,0);
    lv_obj_clear_flag(content,LV_OBJ_FLAG_CLICKABLE);render();
    timer=lv_timer_create(tick,250,NULL);lv_obj_add_event_cb(root,deleted,LV_EVENT_DELETE,NULL);return root;
}
static void open_app(lv_obj_t * view)
{
    (void)view;active=true;last_auto=time(NULL);hachimi_weather_poll();
    const hachimi_weather_t * d=hachimi_weather_data();
    if(!d->fetched[0]||time(NULL)-d->fetched[0]>=1800)hachimi_weather_refresh();
    render();
}
static void close_app(lv_obj_t * view){(void)view;active=false;close_overlay(NULL);}
const hachimi_app_module_t hachimi_weather_app={"weather",create,open_app,close_app,NULL,hachimi_weather_shutdown};
