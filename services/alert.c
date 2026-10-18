#define _POSIX_C_SOURCE 200809L
#include "alert.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#define ALERT_RATE 22050u
#define ALERT_FRAMES (ALERT_RATE * 3u / 2u)
#define ALERT_HEADER_SIZE 44u

/* The generated PCM stays alive while the asynchronous backend consumes it. */
static struct {
    uint8_t header[ALERT_HEADER_SIZE];
    int16_t samples[ALERT_FRAMES];
} tone;
static bool tone_ready;
static bool active;

static void put_u16(uint8_t * output, unsigned value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t * output, unsigned value)
{
    for(unsigned i = 0; i < 4; i++) output[i] = (uint8_t)(value >> (i * 8));
}

static void prepare_tone(void)
{
    if(tone_ready) return;
    memcpy(tone.header, "RIFF", 4);
    put_u32(tone.header + 4, ALERT_FRAMES * 2u + 36u);
    memcpy(tone.header + 8, "WAVEfmt ", 8);
    put_u32(tone.header + 16, 16);
    put_u16(tone.header + 20, 1);
    put_u16(tone.header + 22, 1);
    put_u32(tone.header + 24, ALERT_RATE);
    put_u32(tone.header + 28, ALERT_RATE * 2u);
    put_u16(tone.header + 32, 2);
    put_u16(tone.header + 34, 16);
    memcpy(tone.header + 36, "data", 4);
    put_u32(tone.header + 40, ALERT_FRAMES * 2u);
    for(unsigned i = 0; i < ALERT_FRAMES; i++) {
        double seconds = (double)i / ALERT_RATE;
        double pulse = seconds < 0.34 ? seconds : seconds - 0.34;
        double amplitude = seconds < 0.18 || (seconds >= 0.34 && seconds < 0.52) ? 0.22 : 0.0;
        if(pulse < 0.008) amplitude *= pulse / 0.008;
        if(pulse > 0.16) amplitude *= (0.18 - pulse) / 0.02;
        tone.samples[i] = (int16_t)(sin(seconds * 880.0 * 6.283185307179586) * amplitude * 32767.0);
    }
    tone_ready = true;
}

typedef BOOL (WINAPI * play_sound_fn)(LPCSTR, HMODULE, DWORD);
static HMODULE sound_library;
static play_sound_fn play_sound;

bool hachimi_alert_start(void)
{
    if(active) return true;
    prepare_tone();
    sound_library = LoadLibraryA("winmm.dll");
    if(sound_library == NULL) return false;
    play_sound = (play_sound_fn)(void *)GetProcAddress(sound_library, "PlaySoundA");
    active = play_sound != NULL && play_sound((LPCSTR)&tone, NULL,
                                               0x0004u | 0x0001u | 0x0008u | 0x0002u);
    if(!active) hachimi_alert_stop();
    return active;
}

void hachimi_alert_poll(void) { }

void hachimi_alert_stop(void)
{
    if(play_sound != NULL) play_sound(NULL, NULL, 0);
    if(sound_library != NULL) FreeLibrary(sound_library);
    sound_library = NULL;
    play_sound = NULL;
    active = false;
}
#else
/* The board's active buzzer is wired to GPIO19 (high = on). The UI calls poll
 * every 50 ms; no sleeps or GPIO toggling loops may block the UI thread. */
static int buzzer_value_fd = -1;
static bool active;
static bool buzzer_on;
static uint64_t started_ms;

static uint64_t monotonic_ms(void)
{
    struct timespec now;
    if(clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static bool write_attribute(const char * path, const char * value)
{
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if(fd < 0) return false;
    size_t length = strlen(value);
    ssize_t written;
    do { written = write(fd, value, length); } while(written < 0 && errno == EINTR);
    close(fd);
    return written == (ssize_t)length;
}

static bool set_buzzer(bool on)
{
    if(buzzer_value_fd < 0 || lseek(buzzer_value_fd, 0, SEEK_SET) < 0) return false;
    const char value = on ? '1' : '0';
    ssize_t written;
    do { written = write(buzzer_value_fd, &value, 1); } while(written < 0 && errno == EINTR);
    if(written != 1) return false;
    buzzer_on = on;
    return true;
}

bool hachimi_alert_start(void)
{
    if(active) return true;
    unsigned gpio = 19;
    const char * configured = getenv("HACHIMI_BUZZER_GPIO");
    if(configured != NULL && configured[0] != '\0') {
        char * end = NULL;
        errno = 0;
        unsigned long parsed = strtoul(configured, &end, 10);
        if(errno != 0 || end == configured || *end != '\0' || parsed > 4095) return false;
        gpio = (unsigned)parsed;
    }
    char value_path[128], direction_path[128], number[16];
    snprintf(value_path, sizeof(value_path), "/sys/class/gpio/gpio%u/value", gpio);
    snprintf(direction_path, sizeof(direction_path), "/sys/class/gpio/gpio%u/direction", gpio);
    if(access(value_path, F_OK) != 0) {
        snprintf(number, sizeof(number), "%u", gpio);
        if(!write_attribute("/sys/class/gpio/export", number) && access(value_path, F_OK) != 0) return false;
    }
    /* Set output and initial low atomically to avoid an unwanted chirp. */
    if(!write_attribute(direction_path, "low")) return false;
    buzzer_value_fd = open(value_path, O_WRONLY | O_CLOEXEC);
    if(buzzer_value_fd < 0) return false;
    if(!set_buzzer(true)) { hachimi_alert_stop(); return false; }
    active = true;
    started_ms = monotonic_ms();
    return true;
}

void hachimi_alert_poll(void)
{
    if(!active) return;
    uint64_t phase = (monotonic_ms() - started_ms) % 1500u;
    bool on = phase < 180u || (phase >= 340u && phase < 520u);
    if(on != buzzer_on && !set_buzzer(on)) hachimi_alert_stop();
}

void hachimi_alert_stop(void)
{
    if(buzzer_value_fd >= 0) {
        (void)set_buzzer(false);
        close(buzzer_value_fd);
        buzzer_value_fd = -1;
    }
    buzzer_on = false;
    active = false;
}
#endif
