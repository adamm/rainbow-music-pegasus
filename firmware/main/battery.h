#ifndef __BATTERY_H__
#define __BATTERY_H__

#include <stdbool.h>

void battery_init(void);
bool battery_check(void);
void battery_stop(void);

#endif
