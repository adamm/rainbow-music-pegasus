#ifndef __DIGIPOT_H__
#define __DIGIPOT_H__

#include "esp_log.h"

void digipot_init();
void digipot_set_value(int);
void digitpot_stop();

#endif