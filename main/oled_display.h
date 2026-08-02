#pragma once
#include "config.h"
#include "esp_err.h"

esp_err_t oled_init(void);
void oled_update(const player_state_t *state);
void oled_show_message(const char *line1, const char *line2);
void oled_clear(void);
