#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

esp_err_t usb_audio_output_init(void);
bool usb_audio_output_is_connected(void);
esp_err_t usb_audio_output_start_stream(uint32_t sample_rate, uint16_t bits_per_sample, uint16_t num_channels);
esp_err_t usb_audio_output_reconfigure(uint32_t sample_rate, uint16_t bits_per_sample, uint16_t num_channels);
esp_err_t usb_audio_output_write(const void *data, size_t len, size_t *bytes_written);
void usb_audio_output_stop(void);
void usb_audio_output_apply_volume(int16_t *pcm_data, int sample_count, uint8_t volume_percent);
esp_err_t usb_audio_output_set_volume(uint8_t volume_percent);
