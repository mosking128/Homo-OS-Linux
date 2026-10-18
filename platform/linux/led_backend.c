#include "services/led_backend.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static const char * const paths[] = {
    "/sys/class/leds/red/brightness", "/sys/class/leds/green/brightness",
    "/sys/class/leds/blue/brightness"
};

bool hachimi_led_backend_read(int values[3])
{
    int result[3];
    for(unsigned i = 0; i < 3; i++) {
        char text[16];
        int fd = open(paths[i], O_RDONLY | O_CLOEXEC);
        if(fd < 0) return false;
        ssize_t length = read(fd, text, sizeof(text)-1);
        close(fd);
        if(length <= 0) return false;
        text[length] = 0;
        char * end;
        long value = strtol(text, &end, 10);
        if(end == text || value < 0 || value > 255) return false;
        result[i] = (int)value;
    }
    for(unsigned i = 0; i < 3; i++) values[i] = result[i];
    return true;
}

bool hachimi_led_backend_write(unsigned channel, int value)
{
    if(channel >= 3 || value < 0 || value > 255) return false;
    char text[8];
    int length = snprintf(text, sizeof(text), "%d\n", value);
    int fd = open(paths[channel], O_WRONLY | O_CLOEXEC);
    if(fd < 0) return false;
    ssize_t written = write(fd, text, (size_t)length);
    close(fd);
    return written == length;
}
