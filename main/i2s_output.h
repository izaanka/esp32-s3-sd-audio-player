#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

esp_err_t i2s_output_init(uint32_t sample_rate, uint16_t bits_per_sample, uint16_t num_channels);
esp_err_t i2s_output_write(const void *data, size_t len, size_t *bytes_written);
esp_err_t i2s_output_reconfigure(uint32_t sample_rate, uint16_t bits_per_sample, uint16_t num_channels);
void i2s_output_stop(void);
void i2s_output_start(void);
void i2s_output_apply_volume(int16_t *pcm_data, int sample_count, uint8_t volume_percent);
