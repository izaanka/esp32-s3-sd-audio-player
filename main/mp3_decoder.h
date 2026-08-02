#pragma once

#include "esp_err.h"
#include <stdint.h>

typedef struct {
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint16_t num_channels;
    uint32_t duration_ms;
} mp3_info_t;

typedef struct mp3_decoder* mp3_handle_t;

esp_err_t mp3_decoder_open(const char *path, mp3_handle_t *out_handle, mp3_info_t *out_info);
int mp3_decoder_decode_frame(mp3_handle_t handle, int16_t *pcm_out, int *samples_out);
uint32_t mp3_decoder_get_elapsed_ms(mp3_handle_t handle);
void mp3_decoder_close(mp3_handle_t handle);
