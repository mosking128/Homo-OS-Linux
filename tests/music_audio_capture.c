/* Run with ALSA's file -> null plugin to inspect the real worker's output
 * without sending sound to the headphone socket. */
#include "services/music.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc,char ** argv)
{
    if(argc!=3 && argc!=4) return 2;
    hachimi_music_init();
    hachimi_music_state_t s; hachimi_music_state(&s);
    size_t index=(size_t)strtoul(argv[1],NULL,10);
    if(index>=s.count) return 3;
    unsigned volume=(unsigned)strtoul(argv[2],NULL,10);
    hachimi_music_volume(volume);
    hachimi_music_select(index,true);
    unsigned milliseconds=argc==4 ? (unsigned)strtoul(argv[3],NULL,10) : 800;
    if(milliseconds>300000) milliseconds=300000;
    for(unsigned elapsed=0;elapsed<milliseconds;elapsed+=100) usleep(100000);
    hachimi_music_state(&s);
    hachimi_music_set_playing(false);
    usleep(100000);
    hachimi_music_shutdown();
    if(s.error[0] || !s.playing || s.position_ms<100) {
        fprintf(stderr,"capture failed: %s position=%u\n",s.error,s.position_ms); return 1;
    }
    printf("CAPTURE index=%u volume=%u position=%u underruns=%u\n",(unsigned)index,volume,s.position_ms,s.underruns);
    if(s.underruns) return 4;
    return 0;
}
