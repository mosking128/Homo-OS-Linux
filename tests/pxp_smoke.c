#define _POSIX_C_SOURCE 200809L
#include "platform/linux/pxp/pxp_engine.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#define W 800u
#define H 480u
#define BYTES (W*H*2)
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"FAIL line %d: %s (%s)\n",__LINE__,#c,strerror(errno)); exit(1); } } while(0)
static double clock_ms(clockid_t id) { struct timespec t;clock_gettime(id,&t);return t.tv_sec*1000.0+t.tv_nsec/1000000.0; }
static unsigned short pattern(unsigned x,unsigned y) {return (unsigned short)((x*37u)^(y*1031u));}
int main(int argc,char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0);
    if(argc>1 && !strcmp(argv[1],"--unavailable")) {
        CHECK(hachimi_pxp_open("/dev/null",W,H)==NULL);
        CHECK(hachimi_pxp_open("/dev/pxp-does-not-exist",W,H)==NULL);
        puts("PASS unavailable device returns without blocking");return 0;
    }
    hachimi_pxp_t *p=hachimi_pxp_open("/dev/pxp_device",W,H);CHECK(p);
    unsigned short *src=hachimi_pxp_pixels(p),*dst=hachimi_pxp_scratch(p);
    uint32_t target=hachimi_pxp_scratch_address(p);
    for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++)src[y*W+x]=pattern(x,y);
    memset(dst,0,BYTES);
    CHECK(hachimi_pxp_blit(p,0,0,W,H,target,W*2,W,H)==0);
    for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++)CHECK(dst[y*W+x]==pattern(x,y));
    puts("PASS cached DMA source, cache clean and full RGB565 copy: 384000 pixels");
    for(unsigned i=0;i<W*H;i++)dst[i]=0x5aa5;
    CHECK(hachimi_pxp_blit(p,23,17,137,47,target+(13*W+11)*2,W*2,137,47)==0);
    for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++) {
        unsigned short expected=(x>=11&&x<148&&y>=13&&y<60)?pattern(x-11+23,y-13+17):0x5aa5;
        CHECK(dst[y*W+x]==expected);
    }
    puts("PASS odd crop, nonzero source/destination offsets, stride and outside guards");
    for(unsigned i=0;i<W*H;i++)src[i]=0x001f;
    memset(dst,0,BYTES);
    CHECK(hachimi_pxp_blit(p,0,0,W,H,target,400*2,400,240)==0);
    for(unsigned i=0;i<400*240;i++)CHECK(dst[i]==0x001f);
    puts("PASS RGB565 scaling 800x480 -> 400x240");
    CHECK(hachimi_pxp_blit(p,799,0,2,1,target,W*2,2,1)<0 && errno==EINVAL);
    CHECK(hachimi_pxp_blit(p,0,0,0,H,target,W*2,W,H)<0 && errno==EINVAL);
    for(unsigned i=0;i<W*H;i++)src[i]=(unsigned short)i;
    double start=clock_ms(CLOCK_MONOTONIC),cpu=clock_ms(CLOCK_PROCESS_CPUTIME_ID);
    for(unsigned i=0;i<30;i++)CHECK(hachimi_pxp_blit(p,0,0,W,H,target,W*2,W,H)==0);
    printf("PXP cached->WC full screen: elapsed %.3f ms, UI-thread CPU %.3f ms/job (30 jobs)\n",
           (clock_ms(CLOCK_MONOTONIC)-start)/30,(clock_ms(CLOCK_PROCESS_CPUTIME_ID)-cpu)/30);
    start=clock_ms(CLOCK_MONOTONIC);cpu=clock_ms(CLOCK_PROCESS_CPUTIME_ID);
    for(unsigned i=0;i<30;i++)memcpy(dst,src,BYTES);
    printf("CPU cached->WC full screen: elapsed %.3f ms, UI-thread CPU %.3f ms/job (30 jobs)\n",
           (clock_ms(CLOCK_MONOTONIC)-start)/30,(clock_ms(CLOCK_PROCESS_CPUTIME_ID)-cpu)/30);
    hachimi_pxp_close(p);
    for(unsigned i=0;i<3;i++){p=hachimi_pxp_open("/dev/pxp_device",W,H);CHECK(p);hachimi_pxp_close(p);}
    puts("PASS argument bounds and repeated worker/channel/buffer teardown");
    /* Stall the helper before it submits any hardware work. Verify the UI-side
     * deadline without deliberately wedging the BSP's DMA engine again. */
    p=hachimi_pxp_open("/dev/pxp_device",W,H);CHECK(p);
    int worker=hachimi_pxp_worker_id(p);CHECK(worker>0);
    CHECK(kill((pid_t)worker,SIGSTOP)==0);
    int status;CHECK(waitpid((pid_t)worker,&status,WUNTRACED)==worker && WIFSTOPPED(status));
    start=clock_ms(CLOCK_MONOTONIC);
    CHECK(hachimi_pxp_blit(p,0,0,W,H,hachimi_pxp_scratch_address(p),W*2,W,H)<0 && errno==ETIMEDOUT);
    CHECK(clock_ms(CLOCK_MONOTONIC)-start<350);
    CHECK(hachimi_pxp_blit(p,0,0,W,H,hachimi_pxp_scratch_address(p),W*2,W,H)<0 && errno==ENODEV);
    hachimi_pxp_close(p);
    puts("PASS worker stall returns within deadline and disables further hardware jobs");
    return 0;
}
