#define _POSIX_C_SOURCE 200809L
#include "services/alert.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Hardware opt-in: drives the board's GPIO19 active buzzer for two cycles.
 * Run with HACHIMI_BUZZER_GPIO=19. Output remains low after completion. */
static void check(int condition, const char * message)
{
    if(condition) return;
    hachimi_alert_stop();
    fprintf(stderr, "FAIL %s\n", message);
    exit(1);
}

static int read_value(void)
{
    FILE * file = fopen("/sys/class/gpio/gpio19/value", "r");
    if(file == NULL) return -1;
    int value = fgetc(file);
    fclose(file);
    return value == '1' ? 1 : (value == '0' ? 0 : -1);
}

static unsigned long long milliseconds(void)
{
    struct timespec now;
    check(clock_gettime(CLOCK_MONOTONIC, &now) == 0, "monotonic clock");
    return (unsigned long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

int main(void)
{
    atexit(hachimi_alert_stop);
    check(setenv("HACHIMI_BUZZER_GPIO", "19", 1) == 0, "select board buzzer");
    unsigned long long start = milliseconds();
    check(hachimi_alert_start(), "open GPIO19");
    check(milliseconds() - start < 100, "start is nonblocking");
    check(read_value() == 1, "first beep starts high");
    check(hachimi_alert_start(), "repeated start is idempotent");
    start = milliseconds();
    int checked[5] = {0};
    const unsigned at_ms[] = {220, 400, 600, 1400, 1580};
    const int expected[] = {0, 1, 0, 0, 1};
    for(;;) {
        struct timespec delay = {0, 10000000};
        nanosleep(&delay, NULL);
        hachimi_alert_poll();
        unsigned elapsed = (unsigned)(milliseconds() - start);
        for(unsigned i = 0; i < 5; i++) {
            if(!checked[i] && elapsed >= at_ms[i]) {
                check(read_value() == expected[i], "beep/pause phase readback");
                checked[i] = 1;
                printf("PASS %u ms GPIO19=%d\n", elapsed, expected[i]);
            }
        }
        if(elapsed >= 1600) break;
    }
    hachimi_alert_stop();
    check(read_value() == 0, "stop immediately drives low");
    hachimi_alert_stop();
    puts("PASS nonblocking double beep, repetition and stop-to-low");
    return 0;
}
