#include "weather.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <direct.h>
#define mkdir_private(p) _mkdir(p)
static HANDLE thread;
#else
#include <pthread.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#define mkdir_private(p) mkdir(p,0700)
static pthread_t thread;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static bool finished;
#endif

/* Small structural JSON reader: direct-child lookup prevents a nested value
 * with the same name from being mistaken for a field of another object. */
typedef struct {int start,end,parent,next; char type;} token_t;
typedef struct {const char * s; int pos,count; token_t t[3072];} json_t;
static int whitespace(json_t * j){while(isspace((unsigned char)j->s[j->pos]))j->pos++;return j->s[j->pos];}
static int value(json_t * j,int parent,int depth)
{
    if(depth>24||j->count>=3072)return -1;
    int c=whitespace(j),n=j->count++;
    token_t * t=&j->t[n];t->start=j->pos;t->parent=parent;t->type=(char)c;
    if(c=='{'||c=='['){
        int close=c=='{'?'}':']';j->pos++;
        if(whitespace(j)!=close)for(;;){
            if(c=='{'){
                if(whitespace(j)!='"'||value(j,n,depth+1)<0||whitespace(j)!=':')return -1;
                j->pos++;
            }
            if(value(j,n,depth+1)<0)return -1;
            int sep=whitespace(j);if(sep==close)break;
            if(sep!=',')return -1;
            j->pos++;
        }
        j->pos++;
    }else if(c=='"'){
        j->pos++;
        while(j->s[j->pos]&&j->s[j->pos]!='"'){
            unsigned char ch=(unsigned char)j->s[j->pos++];
            if(ch<32)return -1;
            if(ch=='\\'){
                ch=(unsigned char)j->s[j->pos++];
                if(ch=='u'){for(int k=0;k<4;k++)if(!isxdigit((unsigned char)j->s[j->pos++]))return -1;}
                else if(!ch||!strchr("\"\\/bfnrt",ch))return -1;
            }
        }
        if(j->s[j->pos]!='"')return -1;
        j->pos++;
    }else{
        while(j->s[j->pos]&&!strchr(",]} \t\r\n",j->s[j->pos]))j->pos++;
        int len=j->pos-t->start;
        if(!len)return -1;
        if(!((len==4&&!strncmp(j->s+t->start,"null",4))||(len==4&&!strncmp(j->s+t->start,"true",4))||
             (len==5&&!strncmp(j->s+t->start,"false",5)))){
            char * end; strtod(j->s+t->start,&end);if(end!=j->s+j->pos)return -1;
        }
    }
    t->end=j->pos;t->next=j->count;return n;
}
static void string(const json_t * j,int n,char * out,size_t cap)
{
    size_t len=0;out[0]=0;if(n<0||!cap)return;
    int p=j->t[n].start,e=j->t[n].end;
    if(j->t[n].type=='"'){p++;e--;}
    if(e-p==4&&!strncmp(j->s+p,"null",4))return;
    while(p<e){
        unsigned char bytes[4];unsigned count=1;bytes[0]=(unsigned char)j->s[p++];
        if(bytes[0]=='\\'){
            char c=j->s[p++];
            if(c=='u'){
                unsigned u=0;for(int k=0;k<4;k++){char h=j->s[p++];u=u*16+(h<='9'?h-'0':tolower(h)-'a'+10);}
                if(u>=0xd800&&u<=0xdfff)u=0xfffd;
                if(u<0x80)bytes[0]=(unsigned char)u;
                else if(u<0x800){count=2;bytes[0]=0xc0|(u>>6);bytes[1]=0x80|(u&63);}
                else{count=3;bytes[0]=0xe0|(u>>12);bytes[1]=0x80|((u>>6)&63);bytes[2]=0x80|(u&63);}
            }else{const char * esc="bfnrt",* found=strchr(esc,c);bytes[0]=found?(unsigned char)"\b\f\n\r\t"[found-esc]:(unsigned char)c;}
        }else if(bytes[0]>=0xc0){
            count=bytes[0]<0xe0?2:bytes[0]<0xf0?3:4;
            for(unsigned k=1;k<count;k++)bytes[k]=p<e?(unsigned char)j->s[p++]:0;
        }
        if(len+count>=cap)break;
        memcpy(out+len,bytes,count);len+=count;
    }
    out[len]=0;
}
static int field(const json_t * j,int parent,const char * key)
{
    if(parent<0||j->t[parent].type!='{')return -1;
    for(int n=parent+1;n<j->t[parent].next;){
        int v=n+1;char name[64];string(j,n,name,sizeof(name));
        if(!strcmp(name,key))return v;
        n=j->t[v].next;
    }return -1;
}
static void get(const json_t * j,int p,const char * key,char * out,size_t cap){string(j,field(j,p,key),out,cap);}
static bool number(const json_t * j,int p,const char * key,int * out,int min,int max)
{
    char b[32],* end;get(j,p,key,b,sizeof(b));if(!b[0])return false;
    long n=strtol(b,&end,10);if(*end||n<min||n>max)return false;*out=(int)n;return true;
}
bool hachimi_weather_decode(hachimi_weather_t * out,unsigned endpoint,const char * body)
{
    if(endpoint>2||!body)return false;
    out->errors[endpoint][0]=0;
    json_t * j=calloc(1,sizeof(*j));if(!j)return false;j->s=body;
    bool ok=false;
    if(value(j,-1,0)<0||whitespace(j))goto done;
    int results=field(j,0,"results"),r=-1;
    if(results>=0&&j->t[results].type=='['&&j->t[results].next>results+1)r=results+1;
    if(r<0){
        char status[32];get(j,0,"status_code",status,sizeof(status));
        const char * msg="天气服务暂不可用";
        if(!strcmp(status,"AP010003"))msg="密钥无效，请检查配置";
        else if(!strcmp(status,"AP010014"))msg="请求过于频繁，请稍后刷新";
        else if(!strcmp(status,"AP010002"))msg="此接口暂无访问权限";
        else if(!strcmp(status,"AP010006"))msg="此城市暂无访问权限";
        else if(!strcmp(status,"AP010010")||!strcmp(status,"AP010011"))msg="未找到城市，请试试城市拼音";
        else if(!strcmp(status,"AP010013"))msg="请求额度已用完，请稍后重试";
        else if(!strcmp(status,"AP010012"))msg="天气服务已过期，请检查账号";
        snprintf(out->errors[endpoint],sizeof(out->errors[endpoint]),"%s%s%s",msg,status[0]?" · ":"",status);
        goto done;
    }
    if(endpoint==0){
        int now=field(j,r,"now"),temp,code;
        char name[64],text[48],updated[48];get(j,field(j,r,"location"),"name",name,sizeof(name));
        get(j,now,"text",text,sizeof(text));get(j,r,"last_update",updated,sizeof(updated));
        if(!name[0]||!text[0]||!number(j,now,"temperature",&temp,-100,80)||!number(j,now,"code",&code,0,999))goto done;
        snprintf(out->city,sizeof(out->city),"%s",name);snprintf(out->text,sizeof(out->text),"%s",text);
        snprintf(out->updated,sizeof(out->updated),"%s",updated);out->temperature=temp;out->code=code;out->now_valid=true;ok=true;
    }else if(endpoint==1){
        int d=field(j,r,"daily");hachimi_weather_day_t days[3]={0};unsigned count=0;
        if(d<0||j->t[d].type!='[')goto done;
        for(int n=d+1;n<j->t[d].next&&count<3;n=j->t[n].next){
            hachimi_weather_day_t * day=&days[count];
            get(j,n,"date",day->date,sizeof(day->date));get(j,n,"text_day",day->day,sizeof(day->day));get(j,n,"text_night",day->night,sizeof(day->night));
            if(strlen(day->date)!=10||!day->day[0]||!number(j,n,"low",&day->low,-100,80)||!number(j,n,"high",&day->high,-100,80)||day->low>day->high||!number(j,n,"code_day",&day->code,0,999))goto done;
            get(j,n,"wind_direction",day->wind,sizeof(day->wind));get(j,n,"wind_speed",day->speed,sizeof(day->speed));get(j,n,"wind_scale",day->scale,sizeof(day->scale));count++;
        }
        if(!count)goto done;
        memcpy(out->daily,days,sizeof(days));out->days=count;ok=true;
    }else{
        int s=field(j,r,"suggestion");if(s>=0&&j->t[s].type=='[')s=j->t[s].next>s+1?s+1:-1;
        if(s<0||j->t[s].type!='{')goto done;
        static const char * keys[]={"dressing","uv","car_washing","comfort","flu","sport"};
        static const char * names[]={"穿衣","紫外线","洗车","舒适度","感冒","运动"};
        char brief[6][64]={0},details[6][384]={0};
        char life_names[6][24]={0};
        for(unsigned k=0;k<6;k++){
            int entry=field(j,s,keys[k]);const char * name=names[k];
            get(j,entry,"brief",brief[k],64);
            if(k==3&&!brief[k][0]){entry=field(j,s,"travel");name="旅游";get(j,entry,"brief",brief[k],64);}
            get(j,entry,"details",details[k],384);snprintf(life_names[k],24,"%s",name);if(brief[k][0])ok=true;
        }
        if(ok){memcpy(out->brief,brief,sizeof(brief));memcpy(out->details,details,sizeof(details));memcpy(out->life_names,life_names,sizeof(life_names));}
    }
done:
    if(ok){out->fetched[endpoint]=time(NULL);out->errors[endpoint][0]=0;}
    else if(!out->errors[endpoint][0])snprintf(out->errors[endpoint],128,"天气数据不完整，请稍后重试");
    free(j);return ok;
}

static hachimi_weather_t data,pending;
static bool initialized,running;
static time_t last_request;
static char location[128]="beijing",key[128],state_dir[1024];
static const char * const paths[]={"weather/now","weather/daily","life/suggestion"};
static void filename(char * out,size_t cap,const char * name){snprintf(out,cap,"%s/%s",state_dir,name);}
static bool valid_location(const char * s)
{
    if(!s||!*s||strlen(s)>=sizeof(location))return false;
    for(const unsigned char * p=(const unsigned char*)s;*p;p++)if(*p<32||*p==127)return false;
    return true;
}
static void init(void)
{
    if(initialized)return;
    initialized=true;
    const char * dir=getenv("HACHIMI_WEATHER_DIR"),* home=getenv("HOME");
#ifdef _WIN32
    if(!home)home=getenv("USERPROFILE");
#endif
    if(dir)snprintf(state_dir,sizeof(state_dir),"%s",dir);
    else snprintf(state_dir,sizeof(state_dir),"%s/.hachimi-weather",home?home:".");
    mkdir_private(state_dir);
    char path[1200],line[128];filename(path,sizeof(path),"location");FILE * f=fopen(path,"rb");
    if(f){if(fgets(line,sizeof(line),f)){line[strcspn(line,"\r\n")]=0;if(valid_location(line))snprintf(location,sizeof(location),"%s",line);}fclose(f);}
    snprintf(data.location,sizeof(data.location),"%s",location);
    /* Cache stores original JSON; it is always validated by the same decoder. */
    for(unsigned i=0;i<3;i++){
        char name[32],saved_location[128];long long saved_time=0;
        snprintf(name,sizeof(name),"cache-%u.json",i);filename(path,sizeof(path),name);f=fopen(path,"rb");
        if(!f)continue;
        if(fgets(saved_location,sizeof(saved_location),f)&&fscanf(f,"%lld\n",&saved_time)==1){
            saved_location[strcspn(saved_location,"\r\n")]=0;
            char body[32768];size_t n=fread(body,1,sizeof(body)-1,f);body[n]=0;
            if(!strcmp(saved_location,location)&&!ferror(f)&&feof(f)&&hachimi_weather_decode(&data,i,body))data.fetched[i]=(time_t)saved_time;
        }fclose(f);
    }
}
const char * hachimi_weather_location(void){init();return location;}
const hachimi_weather_t * hachimi_weather_data(void){init();return &data;}
bool hachimi_weather_busy(void){return running;}
bool hachimi_weather_select(const char * city)
{
    init();if(running||!valid_location(city))return false;
    if(!strcmp(city,location))return true;
    snprintf(location,sizeof(location),"%s",city);memset(&data,0,sizeof(data));snprintf(data.location,sizeof(data.location),"%s",city);last_request=0;
    char path[1200];filename(path,sizeof(path),"location");FILE * f=fopen(path,"wb");if(f){fprintf(f,"%s\n",location);fclose(f);}return true;
}
static void encode(const char * in,char * out)
{
    static const char hex[]="0123456789ABCDEF";
    for(const unsigned char * p=(const unsigned char*)in;*p;p++){
        if((isalnum(*p)&&*p<128)||strchr("-_.~",*p))*out++=(char)*p;
        else{*out++='%';*out++=hex[*p>>4];*out++=hex[*p&15];}
    }*out=0;
}
static bool fetch(const char * url,char * body,size_t cap)
{
    size_t used=0;bool ok=false;
#ifdef _WIN32
    wchar_t path[2048];const char * suffix=strstr(url,"/v3/");if(!suffix)return false;
    MultiByteToWideChar(CP_UTF8,0,suffix,-1,path,2048);
    HINTERNET session=WinHttpOpen(L"HachimiWeather/1.0",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!session)return false;
    WinHttpSetTimeouts(session,5000,5000,5000,8000);
    HINTERNET connection=WinHttpConnect(session,L"api.seniverse.com",INTERNET_DEFAULT_HTTPS_PORT,0),request=NULL;
    if(connection)request=WinHttpOpenRequest(connection,L"GET",path,NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
    if(request&&WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(request,NULL)){
        DWORD n;ok=true;
        while(used<cap-1){if(!WinHttpReadData(request,body+used,(DWORD)(cap-1-used),&n)){ok=false;break;}if(!n)break;used+=n;}
        if(used==cap-1)ok=false;
    }
    if(request)WinHttpCloseHandle(request);
    if(connection)WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
#else
    int input[2],output[2];if(pipe(input))return false;if(pipe(output)){close(input[0]);close(input[1]);return false;}
    pid_t pid=fork();
    if(pid==0){
        dup2(input[0],STDIN_FILENO);dup2(output[1],STDOUT_FILENO);
        int null=open("/dev/null",O_WRONLY);if(null>=0){dup2(null,STDERR_FILENO);close(null);}
        close(input[0]);close(input[1]);close(output[0]);close(output[1]);
        execlp("curl","curl","--silent","--show-error","--connect-timeout","5","--max-time","14","--max-filesize","32767","--proto","=https","--config","-",(char*)NULL);_exit(127);
    }
    close(input[0]);close(output[1]);
    if(pid<0){close(input[1]);close(output[0]);return false;}
    /* URL (including the private key) travels through stdin, never argv. */
    char config[2300];int len=snprintf(config,sizeof(config),"url = \"%s\"\n",url);
    /* Child retains stdin until exec; curl is required on the target. */
    if(write(input[1],config,(size_t)len)!=len){close(input[1]);close(output[0]);waitpid(pid,NULL,0);return false;}
    close(input[1]);
    ssize_t n;while(used<cap-1&&(n=read(output[0],body+used,cap-1-used))!=0){if(n<0){if(errno==EINTR)continue;break;}used+=(size_t)n;}
    close(output[0]);int status=0;while(waitpid(pid,&status,0)<0&&errno==EINTR){}ok=WIFEXITED(status)&&WEXITSTATUS(status)==0&&used<cap-1;
#endif
    body[used]=0;return ok&&used>0;
}
static void save(unsigned i,const char * body)
{
    char name[32],path[1200],temp[1210];snprintf(name,sizeof(name),"cache-%u.json",i);filename(path,sizeof(path),name);snprintf(temp,sizeof(temp),"%s.tmp",path);
    FILE * f=fopen(temp,"wb");if(!f)return;
    bool ok=fprintf(f,"%s\n%lld\n%s",pending.location,(long long)pending.fetched[i],body)>0;
    if(fclose(f))ok=false;
    if(ok){
#ifdef _WIN32
        MoveFileExA(temp,path,MOVEFILE_REPLACE_EXISTING);
#else
        rename(temp,path);
#endif
    }
}
#ifdef _WIN32
static DWORD WINAPI worker(void * unused)
#else
static void * worker(void * unused)
#endif
{
    (void)unused;char encoded_key[384],encoded_location[384];encode(key,encoded_key);encode(pending.location,encoded_location);
#ifndef _WIN32
    sigset_t blocked;sigemptyset(&blocked);sigaddset(&blocked,SIGPIPE);pthread_sigmask(SIG_BLOCK,&blocked,NULL);
#endif
    for(unsigned i=0;i<3;i++){
        char url[1800],body[32768];
        snprintf(url,sizeof(url),"https://api.seniverse.com/v3/%s.json?key=%s&location=%s&language=zh-Hans&unit=c&start=0&days=3",paths[i],encoded_key,encoded_location);
        if(fetch(url,body,sizeof(body))){if(hachimi_weather_decode(&pending,i,body))save(i,body);}
        else snprintf(pending.errors[i],128,"连接失败，请检查网络后刷新");
        /* Free accounts allow one request per second. */
        if(i<2){
#ifdef _WIN32
            Sleep(1100);
#else
            usleep(1100000);
#endif
        }
    }
    memset(key,0,sizeof(key));
#ifndef _WIN32
    pthread_mutex_lock(&lock);finished=true;pthread_mutex_unlock(&lock);
#endif
    return 0;
}
bool hachimi_weather_refresh(void)
{
    init();if(running)return false;
    time_t now=time(NULL);if(last_request&&now>=last_request&&now-last_request<60)return false;
    const char * env=getenv("HACHIMI_WEATHER_KEY");key[0]=0;
    if(env)snprintf(key,sizeof(key),"%s",env);
    else{char path[1200];filename(path,sizeof(path),"weather.key");FILE * f=fopen(path,"rb");if(f){if(!fgets(key,sizeof(key),f))key[0]=0;fclose(f);key[strcspn(key,"\r\n")]=0;}}
    if(!key[0]){snprintf(data.errors[0],128,"请先配置天气 API 密钥");return false;}
    pending=data;last_request=now;
#ifdef _WIN32
    thread=CreateThread(NULL,0,worker,NULL,0,NULL);running=thread!=NULL;
#else
    pthread_mutex_lock(&lock);finished=false;pthread_mutex_unlock(&lock);
    running=pthread_create(&thread,NULL,worker,NULL)==0;
#endif
    if(!running)snprintf(data.errors[0],128,"无法启动天气请求，请重试");
    return running;
}
bool hachimi_weather_poll(void)
{
    if(!running)return false;
#ifdef _WIN32
    if(WaitForSingleObject(thread,0)!=WAIT_OBJECT_0)return false;
    CloseHandle(thread);thread=NULL;
#else
    /* pthread_tryjoin_np is unavailable on older target C libraries. A mutex
     * protects only the completion flag; LVGL never touches worker storage. */
    pthread_mutex_lock(&lock);bool done=finished;pthread_mutex_unlock(&lock);
    if(!done)return false;
    pthread_join(thread,NULL);
#endif
    data=pending;running=false;return true;
}
void hachimi_weather_shutdown(void)
{
    if(!running)return;
#ifdef _WIN32
    WaitForSingleObject(thread,INFINITE);CloseHandle(thread);thread=NULL;
#else
    pthread_join(thread,NULL);
#endif
    data=pending;running=false;
}
