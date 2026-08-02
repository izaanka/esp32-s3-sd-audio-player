#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t sd_card_init(void);
void sd_card_deinit(void);
int sd_card_scan_audio(void);
bool sd_card_is_mounted(void);
