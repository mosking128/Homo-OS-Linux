#ifndef HACHIMI_LED_BACKEND_H
#define HACHIMI_LED_BACKEND_H
#include <stdbool.h>
bool hachimi_led_backend_read(int values[3]);
bool hachimi_led_backend_write(unsigned channel, int value);
#endif
