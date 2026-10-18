#include "clock.h"
#include <time.h>

bool hachimi_clock_format(char * destination, size_t capacity, const char * format)
{
    time_t now = time(NULL);
    struct tm * local = localtime(&now);
    return local != NULL && strftime(destination, capacity, format, local) != 0;
}
