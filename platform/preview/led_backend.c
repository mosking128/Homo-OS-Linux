/* No hardware access in previews or headless UI tests. */
#include "services/led_backend.h"
bool hachimi_led_backend_read(int values[3]) { (void)values; return false; }
bool hachimi_led_backend_write(unsigned channel, int value)
{ (void)channel; (void)value; return false; }
