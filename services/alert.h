#ifndef HACHIMI_ALERT_SERVICE_H
#define HACHIMI_ALERT_SERVICE_H

#include <stdbool.h>

/* Nonblocking "beep, beep, pause": GPIO buzzer on the board, audio preview on
 * Windows. False means the output could not be opened. Stop always drives low. */
bool hachimi_alert_start(void);
void hachimi_alert_poll(void);
void hachimi_alert_stop(void);

#endif
