#include "music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#ifndef _WIN32
#include <pthread.h>
#include <dlfcn.h>
#include <unistd.h>
#endif

static hachimi_music_track_t tracks[HACHIMI_MUSIC_MAX];
static hachimi_music_state_t state;
static bool initialized;
#ifndef _WIN32
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t worker;
static bool worker_started, quitting;
static unsigned generation;
#define LOCK() pthread_mutex_lock(&mutex)
#define UNLOCK() pthread_mutex_unlock(&mutex)
#else
#define LOCK() ((void)0)
#define UNLOCK() ((void)0)
#endif
static uint32_t le32(const unsigned char * p) { return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static unsigned le16(const unsigned char * p) { return p[0] | (unsigned)p[1]<<8; }

bool hachimi_music_read_wav(const char * path, hachimi_music_track_t * t)
{
    FILE * f = fopen(path, "rb");
    if(!f) return false;
    unsigned char h[16];
    bool fmt = false, data = false;
    memset(t, 0, sizeof(*t));
    if(fread(h, 1, 12, f) != 12 || memcmp(h,"RIFF",4) || memcmp(h+8,"WAVE",4)) goto done;
    struct stat st;
    if(fstat(fileno(f), &st)) goto done;
    while(fread(h, 1, 8, f) == 8) {
        uint32_t n = le32(h+4);
        long pos = ftell(f);
        if(pos < 0 || (uint64_t)pos+n > (uint64_t)st.st_size) break;
        if(!memcmp(h,"fmt ",4)) {
            if(n < 16 || fread(h, 1, 16, f) != 16) break;
            t->channels = le16(h+2); t->rate = le32(h+4);
            fmt = le16(h)==1 && le16(h+14)==16 && (t->channels==1 || t->channels==2)
                && t->rate>=8000 && t->rate<=192000 && le16(h+12)==t->channels*2;
        }
        else if(!memcmp(h,"data",4)) {
            t->data_offset = (uint32_t)pos; t->data_bytes = n; data = true;
        }
        if(fmt && data) break;
        if(fseek(f, pos+(long)n+(n&1), SEEK_SET)) break;
    }
done:
    fclose(f);
    if(!fmt || !data || !t->data_bytes || t->data_bytes%(t->channels*2)) return false;
    if(strlen(path)>=sizeof(t->path)) return false;
    strcpy(t->path,path);
    t->duration_ms = (uint64_t)t->data_bytes*1000/(t->rate*t->channels*2);
    const char * base = strrchr(path,'/');
    base = base ? base+1 : path;
    snprintf(t->title,sizeof(t->title),"%s",base);
    char * ext = strrchr(t->title,'.'); if(ext) *ext=0;
    char * sep = strstr(t->title," - ");
    if(sep) {
        *sep=0; snprintf(t->artist,sizeof(t->artist),"%.127s",t->title);
        memmove(t->title,sep+3,strlen(sep+3)+1);
    }
    else strcpy(t->artist,"未知歌手");
    if(base>path) {
        size_t len=(size_t)(base-path-1); const char * start=path+len;
        while(start>path && start[-1]!='/') start--;
        snprintf(t->album,sizeof(t->album),"%.*s",(int)(path+len-start),start);
    }
    return true;
}

static int line_cmp(const void * a, const void * b)
{
    int64_t x=((const hachimi_lrc_line_t*)a)->ms, y=((const hachimi_lrc_line_t*)b)->ms;
    return (x>y)-(x<y);
}
bool hachimi_music_read_lrc(const char * path, hachimi_lrc_t * lrc)
{
    memset(lrc,0,sizeof(*lrc));
    FILE * f=fopen(path,"rb"); if(!f) return false;
    char line[4096];
    while(fgets(line,sizeof(line),f)) {
        char * p=line;
        if(!strncmp(p,"\xef\xbb\xbf",3)) p+=3;
        if(!strncmp(p,"[offset:",8)) { lrc->offset_ms=atoi(p+8); continue; }
        int64_t times[32]; size_t nt=0;
        while(*p=='[' && nt<32) {
            char * end=strchr(p,']'); if(!end) break;
            unsigned m,s; int used=0;
            if(sscanf(p,"[%u:%u%n",&m,&s,&used)!=2 || s>=60 || m>9999) break;
            char * tail=p+used; unsigned frac=0;
            if(*tail=='.' || *tail==',') {
                tail++; unsigned scale=100;
                while(*tail>='0' && *tail<='9') { if(scale) { frac+=(*tail-'0')*scale; scale/=10; } tail++; }
            }
            if(tail!=end) break;
            times[nt++]=(int64_t)m*60000+s*1000+frac; p=end+1;
        }
        if(!nt) continue;
        char clean[768]; size_t k=0;
        /* Enhanced LRC word timestamps are optional; show the plain lyric line. */
        while(*p && *p!='\r' && *p!='\n' && k+1<sizeof(clean)) {
            if(*p=='<' && strchr(p,'>')) { p=strchr(p,'>')+1; continue; }
            clean[k++]=*p++;
        }
        clean[k]=0;
        for(size_t i=0;i<nt && lrc->count<HACHIMI_LRC_MAX;i++) {
            hachimi_lrc_line_t * x=&lrc->lines[lrc->count++];
            x->ms=times[i]; strcpy(x->text,clean);
        }
    }
    fclose(f);
    qsort(lrc->lines,lrc->count,sizeof(lrc->lines[0]),line_cmp);
    return lrc->count>0;
}
int hachimi_music_lyric_at(const hachimi_lrc_t * lrc, uint32_t ms)
{
    int found=-1;
    /* A positive LRC offset advances lyrics relative to audio. */
    int64_t time=(int64_t)ms+lrc->offset_ms;
    for(size_t i=0;i<lrc->count && lrc->lines[i].ms<=time;i++) found=(int)i;
    return found;
}
static void scan(const char * path, unsigned depth)
{
    if(depth>5 || state.count>=HACHIMI_MUSIC_MAX) return;
    DIR * d=opendir(path); if(!d) return;
    struct dirent * e;
    while((e=readdir(d)) && state.count<HACHIMI_MUSIC_MAX) {
        if(e->d_name[0]=='.') continue;
        char full[1024];
        if(snprintf(full,sizeof(full),"%s/%s",path,e->d_name)>=(int)sizeof(full)) continue;
        struct stat st;
        #ifdef _WIN32
        if(stat(full,&st)) continue;
#else
        if(lstat(full,&st)) continue;
#endif
        if(S_ISDIR(st.st_mode)) scan(full,depth+1);
        else if(S_ISREG(st.st_mode)) {
            const char * ext=strrchr(full,'.');
            if(ext && !strcasecmp(ext,".wav") && hachimi_music_read_wav(full,&tracks[state.count])) state.count++;
        }
    }
    closedir(d);
}
static int track_cmp(const void * a,const void * b) { return strcmp(((const hachimi_music_track_t*)a)->path,((const hachimi_music_track_t*)b)->path); }

#ifndef _WIN32
/* Stable ALSA public ABI loaded at runtime: builds with the existing board
 * toolchain without requiring development packages in its old sysroot. */
typedef struct _snd_pcm snd_pcm_t;
typedef struct {
    void * lib;
    int (*open)(snd_pcm_t **,const char *,int,int);
    int (*close)(snd_pcm_t *);
    int (*set_params)(snd_pcm_t *,int,int,unsigned,unsigned,int,unsigned);
    long (*writei)(snd_pcm_t *,const void *,unsigned long);
    int (*delay)(snd_pcm_t *,long *);
    int (*drop)(snd_pcm_t *);
    int (*prepare)(snd_pcm_t *);
    const char * (*strerror)(int);
} alsa_t;
static bool load_alsa(alsa_t * a)
{
    memset(a,0,sizeof(*a)); a->lib=dlopen("libasound.so.2",RTLD_NOW|RTLD_LOCAL);
    if(!a->lib) return false;
#define LOAD(field,name) do { *(void **)(&a->field)=dlsym(a->lib,name); if(!a->field) { dlclose(a->lib); a->lib=NULL; return false; } } while(0)
    LOAD(open,"snd_pcm_open"); LOAD(close,"snd_pcm_close"); LOAD(set_params,"snd_pcm_set_params");
    LOAD(writei,"snd_pcm_writei"); LOAD(delay,"snd_pcm_delay"); LOAD(drop,"snd_pcm_drop");
    LOAD(prepare,"snd_pcm_prepare"); LOAD(strerror,"snd_strerror");
#undef LOAD
    return true;
}
static void audio_error(unsigned gen,const char * error)
{
    LOCK();
    if(gen==generation) { state.playing=false; snprintf(state.error,sizeof(state.error),"%s",error); }
    UNLOCK();
}
static void * audio_worker(void * unused)
{
    (void)unused;
    alsa_t a; bool available=load_alsa(&a);
    snd_pcm_t * pcm=NULL; FILE * file=NULL;
    unsigned active_gen=~0u; uint64_t frame=0,total=0; unsigned rate=0,channels=0;
    bool draining=false;
    int16_t samples[1024]; size_t pending=0,consumed=0;
    for(;;) {
        LOCK(); bool quit=quitting; unsigned gen=generation; hachimi_music_state_t s=state; UNLOCK();
        if(quit) break;
        if(gen!=active_gen || !s.playing) {
            if(pcm) { a.drop(pcm); a.close(pcm); pcm=NULL; }
            if(file) { fclose(file); file=NULL; }
            pending=consumed=0; draining=false; active_gen=gen;
            if(!s.playing) { usleep(10000); continue; }
            if(!available) { audio_error(gen,"无法加载 ALSA 音频库"); continue; }
            const hachimi_music_track_t * t=&tracks[s.index];
            rate=t->rate; channels=t->channels; total=t->data_bytes/(channels*2);
            frame=(uint64_t)s.position_ms*rate/1000; if(frame>total) frame=total;
            file=fopen(t->path,"rb");
            if(!file || fseek(file,t->data_offset+frame*channels*2,SEEK_SET)) { audio_error(gen,"无法读取音乐文件"); continue; }
            const char * device=getenv("HACHIMI_AUDIO_DEVICE");
            int rc=a.open(&pcm,device && *device ? device : "default",0,1); /* playback, nonblocking */
            if(rc>=0) rc=a.set_params(pcm,2,3,channels,rate,1,200000); /* S16_LE, RW_INTERLEAVED, 200 ms */
            if(rc<0) { audio_error(gen,a.strerror(rc)); continue; }
        }
        if(!pcm || !file) { usleep(10000); continue; }
        if(!pending && !draining) {
            size_t want=(size_t)((total-frame)<512 ? total-frame : 512);
            pending=fread(samples,channels*2,want,file); consumed=0;
            if(pending!=want) { audio_error(gen,"音乐文件读取中断"); continue; }
            /* Keep the multiplication and division signed: an unsigned volume
             * used to wrap every negative sample before division. */
            for(size_t i=0;i<pending*channels;i++)
                samples[i]=(int32_t)((int64_t)samples[i]*s.volume*HACHIMI_MUSIC_GAIN_LIMIT_PERCENT/10000);
            if(!pending) draining=true;
        }
        if(pending) {
            long n=a.writei(pcm,samples+consumed*channels,pending);
            if(n>0) { frame+=n; pending-=n; consumed+=n; }
            else if(n==-EPIPE || n==-ESTRPIPE) {
                LOCK(); if(gen==generation) state.underruns++; UNLOCK();
                int rc=a.prepare(pcm);
                if(rc<0) { audio_error(gen,a.strerror(rc)); continue; }
            }
            else if(n<0 && n!=-EAGAIN && n!=-EINTR) { audio_error(gen,a.strerror((int)n)); continue; }
        }
        long delay=0; a.delay(pcm,&delay); if(delay<0) delay=0;
        uint64_t audible=frame>(uint64_t)delay ? frame-delay : 0;
        LOCK();
        if(gen==generation) {
            state.position_ms=(uint64_t)audible*1000/rate;
            if(draining && delay==0) { state.position_ms=state.duration_ms; state.playing=false; state.ended=true; }
        }
        UNLOCK();
        usleep(5000);
    }
    if(pcm) { a.drop(pcm); a.close(pcm); }
    if(file) fclose(file);
    if(a.lib) dlclose(a.lib);
    return NULL;
}
#endif
void hachimi_music_init(void)
{
    if(initialized) return;
    memset(&state,0,sizeof(state)); state.volume=25;
    const char * dir=getenv("HACHIMI_MUSIC_DIR"); scan(dir && *dir ? dir : "music",0);
    qsort(tracks,state.count,sizeof(tracks[0]),track_cmp);
    if(state.count) state.duration_ms=tracks[0].duration_ms;
    else strcpy(state.error,"未找到 WAV 音乐，请将歌曲放入 music 目录");
    initialized=true;
#ifndef _WIN32
    quitting=false; generation=0;
    worker_started=pthread_create(&worker,NULL,audio_worker,NULL)==0;
    if(!worker_started) strcpy(state.error,"无法启动音频线程");
#endif
}
void hachimi_music_shutdown(void)
{
    if(!initialized) return;
#ifndef _WIN32
    LOCK(); quitting=true; UNLOCK();
    if(worker_started) pthread_join(worker,NULL);
    worker_started=false;
#endif
    initialized=false;
}
const hachimi_music_track_t * hachimi_music_track(size_t i) { return i<state.count ? &tracks[i] : NULL; }
void hachimi_music_state(hachimi_music_state_t * out) { LOCK(); *out=state; UNLOCK(); }
static void changed(void)
{
    state.ended=false; state.error[0]=0;
#ifndef _WIN32
    generation++;
    if(state.playing && !worker_started) { state.playing=false; strcpy(state.error,"无法启动音频线程"); }
#else
    if(state.playing) { state.playing=false; strcpy(state.error,"请在 Linux 开发板上播放音频"); }
#endif
}
void hachimi_music_select(size_t i,bool play)
{
    LOCK(); if(i<state.count) { state.index=i; state.position_ms=0; state.duration_ms=tracks[i].duration_ms; state.underruns=0; state.playing=play; changed(); } UNLOCK();
}
void hachimi_music_set_playing(bool play)
{
    LOCK(); if(state.count) { if(state.position_ms>=state.duration_ms && play) state.position_ms=0; state.playing=play; changed(); } UNLOCK();
}
void hachimi_music_seek(uint32_t ms) { LOCK(); state.position_ms=ms>state.duration_ms ? state.duration_ms : ms; changed(); UNLOCK(); }
void hachimi_music_volume(unsigned volume) { LOCK(); state.volume=volume>100 ? 100 : volume; UNLOCK(); }
void hachimi_music_mode(hachimi_music_mode_t mode) { LOCK(); state.mode=mode; UNLOCK(); }
void hachimi_music_next(bool forward)
{
    hachimi_music_state_t s; hachimi_music_state(&s); if(!s.count) return;
    size_t i;
    if(s.mode==HACHIMI_MUSIC_SHUFFLE && forward && s.count>1) i=(s.index+1+rand()%(s.count-1))%s.count;
    else i=(s.index+s.count+(forward ? 1 : -1))%s.count;
    hachimi_music_select(i,s.playing);
}
void hachimi_music_poll(void)
{
    hachimi_music_state_t s; hachimi_music_state(&s);
    if(!s.ended || !s.count) return;
    if(s.mode==HACHIMI_MUSIC_REPEAT) hachimi_music_select(s.index,true);
    else { hachimi_music_next(true); hachimi_music_set_playing(true); }
}
