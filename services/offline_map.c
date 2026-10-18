#include "offline_map.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif
#define PI 3.14159265358979323846
enum { EMPTY, QUEUED, READING, READY, MISSING };
typedef struct {
    int state, z, x, y;
    uint64_t touched;
    uint8_t * pixels;
} tile_t;
struct hachimi_map {
    char path[512], region[96];
    double south, west, north, east, lat, lon;
    int bounds[4][5];
    hachimi_map_place_t places[HACHIMI_MAP_MAX_PLACES];
    unsigned count, loads;
    tile_t tiles[HACHIMI_MAP_CACHE_SIZE];
    uint64_t frame;
    uint32_t version;
    bool running;
#ifdef _WIN32
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE changed;
    HANDLE worker;
#else
    pthread_mutex_t lock;
    pthread_cond_t changed;
    pthread_t worker;
#endif
};
static void lock(hachimi_map_t * m) {
#ifdef _WIN32
    EnterCriticalSection(&m->lock);
#else
    pthread_mutex_lock(&m->lock);
#endif
}
static void unlock(hachimi_map_t * m) {
#ifdef _WIN32
    LeaveCriticalSection(&m->lock);
#else
    pthread_mutex_unlock(&m->lock);
#endif
}
static void signal_worker(hachimi_map_t * m) {
#ifdef _WIN32
    WakeConditionVariable(&m->changed);
#else
    pthread_cond_signal(&m->changed);
#endif
}
#ifdef _WIN32
static DWORD WINAPI loader(void * arg)
#else
static void * loader(void * arg)
#endif
{
    hachimi_map_t * m=arg;
    lock(m);
    while(m->running) {
        tile_t * tile=NULL;
        for(unsigned i=0;i<HACHIMI_MAP_CACHE_SIZE;i++) if(m->tiles[i].state==QUEUED) {tile=&m->tiles[i];break;}
        if(!tile) {
#ifdef _WIN32
            SleepConditionVariableCS(&m->changed,&m->lock,INFINITE);
#else
            pthread_cond_wait(&m->changed,&m->lock);
#endif
            continue;
        }
        tile->state=READING;
        int z=tile->z,x=tile->x,y=tile->y;
        unlock(m);
        if(!tile->pixels) tile->pixels=malloc(HACHIMI_MAP_TILE_BYTES);
        char path[640];snprintf(path,sizeof(path),"%s/tiles/%d/%d/%d.rgb565",m->path,z,x,y);
        bool ok=false;
        FILE * f=tile->pixels?fopen(path,"rb"):NULL;
        if(f) {ok=fread(tile->pixels,1,HACHIMI_MAP_TILE_BYTES,f)==HACHIMI_MAP_TILE_BYTES && fgetc(f)==EOF;fclose(f);}
        lock(m);
        tile->state=ok?READY:MISSING;m->version++;if(ok)m->loads++;
    }
    unlock(m);
    return 0;
}
static void strip_newline(char * text) {text[strcspn(text,"\r\n")]='\0';}
hachimi_map_t * hachimi_map_open(const char * path)
{
    if(!path||strlen(path)>500)return NULL;
    char filename[640];snprintf(filename,sizeof(filename),"%s/region.txt",path);
    FILE * f=fopen(filename,"r");if(!f)return NULL;
    hachimi_map_t * m=calloc(1,sizeof(*m));if(!m){fclose(f);return NULL;}
    char magic[32];bool valid=fgets(magic,sizeof(magic),f)&&!strcmp(magic,"HACHIMI_MAP_V1\n")&&
        fgets(m->region,sizeof(m->region),f)&&
        fscanf(f,"%lf %lf %lf %lf",&m->south,&m->west,&m->north,&m->east)==4&&
        fscanf(f,"%lf %lf",&m->lat,&m->lon)==2;
    for(unsigned i=0;valid&&i<4;i++) valid=fscanf(f,"%d %d %d %d %d",&m->bounds[i][0],&m->bounds[i][1],&m->bounds[i][2],&m->bounds[i][3],&m->bounds[i][4])==5&&m->bounds[i][0]==(int)i+13&&m->bounds[i][1]<=m->bounds[i][3]&&m->bounds[i][2]<=m->bounds[i][4];
    fclose(f);
    if(!valid||!isfinite(m->south)||!isfinite(m->north)||!isfinite(m->west)||!isfinite(m->east)||
       !isfinite(m->lat)||!isfinite(m->lon)||m->south>=m->north||m->west>=m->east||
       m->south< -85||m->north>85||m->west< -180||m->east>180||
       m->lat<m->south||m->lat>m->north||m->lon<m->west||m->lon>m->east) {free(m);return NULL;}
    strip_newline(m->region);strcpy(m->path,path);
    snprintf(filename,sizeof(filename),"%s/places.tsv",path);f=fopen(filename,"r");
    if(f) {
        char line[1024];
        while(m->count<HACHIMI_MAP_MAX_PLACES&&fgets(line,sizeof(line),f)) {
            strip_newline(line);char * fields[5],*p=line;bool complete=true;
            for(unsigned i=0;i<5;i++) {fields[i]=p;if(i<4){p=strchr(p,'\t');if(!p){complete=false;break;}*p++='\0';}}
            if(!complete)continue;
            hachimi_map_place_t * place=&m->places[m->count];char * end;
            place->lat=strtod(fields[0],&end);if(*end||!isfinite(place->lat))continue;
            place->lon=strtod(fields[1],&end);if(*end||!isfinite(place->lon))continue;
            if(place->lat<m->south||place->lat>m->north||place->lon<m->west||place->lon>m->east)continue;
            if(!*fields[3]||strlen(fields[2])>=sizeof(place->category)||strlen(fields[3])>=sizeof(place->name)||strlen(fields[4])>=sizeof(place->alias))continue;
            strcpy(place->category,fields[2]);strcpy(place->name,fields[3]);strcpy(place->alias,fields[4]);m->count++;
        }
        fclose(f);
    }
#ifdef _WIN32
    InitializeCriticalSection(&m->lock);InitializeConditionVariable(&m->changed);m->running=true;
    m->worker=CreateThread(NULL,0,loader,m,0,NULL);
    if(!m->worker){DeleteCriticalSection(&m->lock);free(m);return NULL;}
#else
    if(pthread_mutex_init(&m->lock,NULL)){free(m);return NULL;}
    if(pthread_cond_init(&m->changed,NULL)){pthread_mutex_destroy(&m->lock);free(m);return NULL;}
    m->running=true;
    if(pthread_create(&m->worker,NULL,loader,m)) {pthread_cond_destroy(&m->changed);pthread_mutex_destroy(&m->lock);free(m);return NULL;}
#endif
    return m;
}
void hachimi_map_close(hachimi_map_t * m)
{
    if(!m)return;
    lock(m);m->running=false;signal_worker(m);unlock(m);
#ifdef _WIN32
    WaitForSingleObject(m->worker,INFINITE);CloseHandle(m->worker);DeleteCriticalSection(&m->lock);
#else
    pthread_join(m->worker,NULL);pthread_cond_destroy(&m->changed);pthread_mutex_destroy(&m->lock);
#endif
    for(unsigned i=0;i<HACHIMI_MAP_CACHE_SIZE;i++)free(m->tiles[i].pixels);
    free(m);
}
const char * hachimi_map_region(hachimi_map_t * m){return m?m->region:"地图数据未安装";}
void hachimi_map_center(hachimi_map_t * m,double * lat,double * lon){if(m){*lat=m->lat;*lon=m->lon;}}
void hachimi_map_world(double lat,double lon,int z,double * x,double * y)
{
    if(lat>85)lat=85;
    if(lat< -85)lat=-85;
    double size=256.0*(1U<<z),r=lat*PI/180.0;
    *x=(lon+180.0)/360.0*size;*y=(1.0-asinh(tan(r))/PI)*0.5*size;
}
void hachimi_map_coords(double x,double y,int z,double * lat,double * lon)
{
    double size=256.0*(1U<<z);*lon=x/size*360.0-180.0;*lat=atan(sinh(PI*(1.0-2.0*y/size)))*180.0/PI;
}
void hachimi_map_clamp(hachimi_map_t * m,int z,int width,int height,double * x,double * y)
{
    if(!m)return;
    double x0,y0,x1,y1;hachimi_map_world(m->north,m->west,z,&x0,&y0);hachimi_map_world(m->south,m->east,z,&x1,&y1);
    if(x1-x0<width)*x=(x0+x1)/2;else {*x=fmax(x0+width/2.0,fmin(x1-width/2.0,*x));}
    if(y1-y0<height)*y=(y0+y1)/2;else {*y=fmax(y0+height/2.0,fmin(y1-height/2.0,*y));}
}
double hachimi_map_distance(double a,double b,double c,double d)
{
    double p=(c-a)*PI/180,q=(d-b)*PI/180;
    double h=sin(p/2)*sin(p/2)+cos(a*PI/180)*cos(c*PI/180)*sin(q/2)*sin(q/2);
    return 6371000.0*2*asin(sqrt(fmin(1.0,fmax(0.0,h))));
}
static bool contains(const char * text,const char * query)
{
    if(!*query)return true;
    for(;*text;text++) {const unsigned char *a=(const unsigned char*)text,*b=(const unsigned char*)query;
        while(*a&&*b) {unsigned ca=*a,cb=*b;if(ca>='A'&&ca<='Z')ca+=32;if(cb>='A'&&cb<='Z')cb+=32;if(ca!=cb)break;a++;b++;}
        if(!*b)return true;}
    return false;
}
unsigned hachimi_map_search(hachimi_map_t * m,const char * query,const char * cat,double lat,double lon,unsigned * results,unsigned capacity)
{
    if(!m||!results||!capacity)return 0;
    if(capacity>32)capacity=32;
    unsigned count=0;double distance[32];if(!query)query="";
    for(unsigned i=0;i<m->count;i++) {
        const hachimi_map_place_t * p=&m->places[i];
        if(cat&&*cat&&strcmp(cat,p->category))continue;
        if(!contains(p->name,query)&&!contains(p->alias,query))continue;
        double d=hachimi_map_distance(lat,lon,p->lat,p->lon);unsigned pos=0;
        while(pos<count&&distance[pos]<=d)pos++;
        if(pos>=capacity)continue;
        if(count<capacity)count++;
        for(unsigned j=count-1;j>pos;j--){distance[j]=distance[j-1];results[j]=results[j-1];}
        distance[pos]=d;results[pos]=i;
    }
    return count;
}
const hachimi_map_place_t * hachimi_map_place(hachimi_map_t * m,unsigned index){return m&&index<m->count?&m->places[index]:NULL;}
void hachimi_map_begin_frame(hachimi_map_t * m){if(m){lock(m);m->frame++;unlock(m);}}
const uint8_t * hachimi_map_tile(hachimi_map_t * m,int z,int x,int y,int * slot,bool * settled)
{
    if(settled)*settled=true;
    if(!m||z<13||z>16)return NULL;
    int * bounds=m->bounds[z-13];if(x<bounds[1]||x>bounds[3]||y<bounds[2]||y>bounds[4])return NULL;
    lock(m);tile_t * victim=NULL;int vi=-1;
    for(unsigned i=0;i<HACHIMI_MAP_CACHE_SIZE;i++) {
        tile_t * t=&m->tiles[i];
        if(t->state!=EMPTY&&t->z==z&&t->x==x&&t->y==y) {
            t->touched=m->frame;if(slot)*slot=(int)i;if(settled)*settled=t->state==READY||t->state==MISSING;
            const uint8_t * pixels=t->state==READY?t->pixels:NULL;unlock(m);return pixels;
        }
        if(t->state==READING||t->state==QUEUED)continue;
        if(t->state!=EMPTY&&t->touched+1>=m->frame)continue;
        if(!victim||t->state==EMPTY||t->touched<victim->touched){victim=t;vi=(int)i;}
    }
    if(victim) {victim->state=QUEUED;victim->z=z;victim->x=x;victim->y=y;victim->touched=m->frame;if(slot)*slot=vi;signal_worker(m);}
    if(settled)*settled=false;
    unlock(m);return NULL;
}
void hachimi_map_stats(hachimi_map_t * m,hachimi_map_stats_t * out)
{
    memset(out,0,sizeof(*out));if(!m)return;lock(m);
    out->loads=m->loads;out->version=m->version;
    for(unsigned i=0;i<HACHIMI_MAP_CACHE_SIZE;i++)switch(m->tiles[i].state){case READY:out->ready++;break;case QUEUED:case READING:out->loading++;break;case MISSING:out->missing++;break;default:break;}
    unlock(m);
}
