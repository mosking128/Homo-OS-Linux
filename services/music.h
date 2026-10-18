#ifndef HACHIMI_MUSIC_H
#define HACHIMI_MUSIC_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define HACHIMI_MUSIC_MAX 256
#define HACHIMI_MUSIC_GAIN_LIMIT_PERCENT 20
#define HACHIMI_LRC_MAX 1024
typedef struct {
    char path[1024], title[256], artist[128], album[256];
    uint32_t rate, channels, data_offset, data_bytes, duration_ms;
} hachimi_music_track_t;
typedef struct { int64_t ms; char text[768]; } hachimi_lrc_line_t;
typedef struct { hachimi_lrc_line_t lines[HACHIMI_LRC_MAX]; size_t count; int offset_ms; } hachimi_lrc_t;
typedef enum { HACHIMI_MUSIC_LIST_LOOP, HACHIMI_MUSIC_REPEAT, HACHIMI_MUSIC_SHUFFLE } hachimi_music_mode_t;
typedef struct {
    size_t index, count;
    uint32_t position_ms, duration_ms;
    unsigned volume;
    unsigned underruns;
    bool playing, ended;
    hachimi_music_mode_t mode;
    char error[192];
} hachimi_music_state_t;
bool hachimi_music_read_wav(const char * path, hachimi_music_track_t * track);
bool hachimi_music_read_lrc(const char * path, hachimi_lrc_t * lrc);
int hachimi_music_lyric_at(const hachimi_lrc_t * lrc, uint32_t ms);
void hachimi_music_init(void);
void hachimi_music_shutdown(void);
const hachimi_music_track_t * hachimi_music_track(size_t index);
void hachimi_music_state(hachimi_music_state_t * out);
void hachimi_music_select(size_t index, bool play);
void hachimi_music_set_playing(bool play);
void hachimi_music_seek(uint32_t ms);
void hachimi_music_volume(unsigned volume);
void hachimi_music_mode(hachimi_music_mode_t mode);
void hachimi_music_next(bool forward);
void hachimi_music_poll(void);
#endif
