#pragma once

#include "esp_err.h"
#include <stdint.h>

typedef struct {
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint16_t num_channels;
    uint32_t data_size;
    uint32_t duration_ms;
} wav_info_t;

typedef struct wav_decoder* wav_handle_t;

esp_err_t wav_decoder_open(const char *path, wav_handle_t *out_handle, wav_info_t *out_info);
int wav_decoder_read(wav_handle_t handle, uint8_t *buffer, int max_bytes);
uint32_t wav_decoder_get_elapsed_ms(wav_handle_t handle);
void wav_decoder_close(wav_handle_t handle);
