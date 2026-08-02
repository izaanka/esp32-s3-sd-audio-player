#include "wav_decoder.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static const char *TAG = "WAV_DEC";

struct wav_decoder {
    FILE *file;
    uint32_t data_size;
    uint32_t bytes_read;
    uint32_t byte_rate;
};

esp_err_t wav_decoder_open(const char *path, wav_handle_t *out_handle, wav_info_t *out_info) {
    if (!path || !out_handle || !out_info) return ESP_ERR_INVALID_ARG;
    
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file %s", path);
        return ESP_FAIL;
    }
    setvbuf(f, NULL, _IOFBF, 16384);
    
    uint8_t header[12];
    if (fread(header, 1, 12, f) != 12) {
        ESP_LOGE(TAG, "Failed to read RIFF header");
        fclose(f);
        return ESP_FAIL;
    }
    
    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        ESP_LOGE(TAG, "Invalid WAV format");
        fclose(f);
        return ESP_FAIL;
    }
    
    bool fmt_found = false;
    bool data_found = false;
    
    while (!data_found) {
        uint8_t chunk_header[8];
        if (fread(chunk_header, 1, 8, f) != 8) {
            break;
        }
        
        uint32_t chunk_size = chunk_header[4] | (chunk_header[5] << 8) | (chunk_header[6] << 16) | (chunk_header[7] << 24);
        
        if (memcmp(chunk_header, "fmt ", 4) == 0) {
            uint8_t fmt_data[16];
            size_t to_read = chunk_size > 16 ? 16 : chunk_size;
            if (fread(fmt_data, 1, to_read, f) != to_read) {
                break;
            }
            
            uint16_t audio_format = fmt_data[0] | (fmt_data[1] << 8);
            if (audio_format != 1) {
                ESP_LOGE(TAG, "Unsupported audio format %d (only PCM supported)", audio_format);
                fclose(f);
                return ESP_FAIL;
            }
            
            out_info->num_channels = fmt_data[2] | (fmt_data[3] << 8);
            out_info->sample_rate = fmt_data[4] | (fmt_data[5] << 8) | (fmt_data[6] << 16) | (fmt_data[7] << 24);
            out_info->bits_per_sample = fmt_data[14] | (fmt_data[15] << 8);
            
            if (chunk_size > 16) {
                fseek(f, chunk_size - 16, SEEK_CUR);
            }
            fmt_found = true;
        } else if (memcmp(chunk_header, "data", 4) == 0) {
            out_info->data_size = chunk_size;
            data_found = true;
        } else {
            fseek(f, chunk_size, SEEK_CUR);
        }
    }
    
    if (!fmt_found || !data_found) {
        ESP_LOGE(TAG, "Missing fmt or data chunk");
        fclose(f);
        return ESP_FAIL;
    }
    
    uint32_t byte_rate = out_info->sample_rate * out_info->num_channels * (out_info->bits_per_sample / 8);
    out_info->duration_ms = (uint32_t)(((uint64_t)out_info->data_size * 1000) / byte_rate);
    
    struct wav_decoder *decoder = malloc(sizeof(struct wav_decoder));
    if (!decoder) {
        ESP_LOGE(TAG, "Failed to allocate memory for decoder");
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    
    decoder->file = f;
    decoder->data_size = out_info->data_size;
    decoder->bytes_read = 0;
    decoder->byte_rate = byte_rate;
    
    *out_handle = decoder;
    return ESP_OK;
}

int wav_decoder_read(wav_handle_t handle, uint8_t *buffer, int max_bytes) {
    if (!handle || !buffer) return 0;
    
    int bytes_to_read = max_bytes;
    if (handle->bytes_read + bytes_to_read > handle->data_size) {
        bytes_to_read = handle->data_size - handle->bytes_read;
    }
    
    if (bytes_to_read <= 0) return 0;
    
    size_t read_bytes = fread(buffer, 1, bytes_to_read, handle->file);
    handle->bytes_read += read_bytes;
    
    return read_bytes;
}

uint32_t wav_decoder_get_elapsed_ms(wav_handle_t handle) {
    if (!handle || handle->byte_rate == 0) return 0;
    return (uint32_t)(((uint64_t)handle->bytes_read * 1000) / handle->byte_rate);
}

void wav_decoder_close(wav_handle_t handle) {
    if (!handle) return;
    if (handle->file) {
        fclose(handle->file);
    }
    free(handle);
}
