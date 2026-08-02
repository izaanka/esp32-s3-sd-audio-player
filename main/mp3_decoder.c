#include "mp3_decoder.h"
#include "config.h"
#include "esp_log.h"
#include "mp3dec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "MP3_DEC";

struct mp3_decoder {
    FILE *file;
    HMP3Decoder hMP3Decoder;
    uint8_t read_buf[MP3_READBUF_SIZE];
    int bytes_left;
    uint8_t *read_ptr;
    long file_size;
    long bytes_consumed;
    uint32_t duration_ms;
    uint32_t sample_rate;
};

esp_err_t mp3_decoder_open(const char *path, mp3_handle_t *out_handle, mp3_info_t *out_info) {
    if (!path || !out_handle || !out_info) return ESP_ERR_INVALID_ARG;
    
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file %s", path);
        return ESP_FAIL;
    }
    setvbuf(f, NULL, _IOFBF, 16384);
    
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    struct mp3_decoder *decoder = malloc(sizeof(struct mp3_decoder));
    if (!decoder) {
        ESP_LOGE(TAG, "Failed to allocate decoder");
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    
    decoder->file = f;
    decoder->file_size = file_size;
    decoder->hMP3Decoder = MP3InitDecoder();
    decoder->bytes_left = 0;
    decoder->read_ptr = decoder->read_buf;
    decoder->bytes_consumed = 0;
    
    // Check and skip ID3v2 header if present
    uint8_t id3_hdr[10];
    if (fread(id3_hdr, 1, 10, f) == 10) {
        if (memcmp(id3_hdr, "ID3", 3) == 0) {
            uint32_t tag_size = ((id3_hdr[6] & 0x7F) << 21) |
                                ((id3_hdr[7] & 0x7F) << 14) |
                                ((id3_hdr[8] & 0x7F) << 7)  |
                                 (id3_hdr[9] & 0x7F);
            uint32_t id3_total_len = 10 + tag_size;
            ESP_LOGI(TAG, "ID3v2 tag detected (%lu bytes), skipping...", id3_total_len);
            fseek(f, id3_total_len, SEEK_SET);
            decoder->bytes_consumed = id3_total_len;
        } else {
            fseek(f, 0, SEEK_SET);
        }
    } else {
        fseek(f, 0, SEEK_SET);
    }
    
    // Read initial buffer after ID3 tag
    decoder->bytes_left = fread(decoder->read_buf, 1, MP3_READBUF_SIZE, f);
    decoder->read_ptr = decoder->read_buf;
    
    // Scan for first sync word, looping buffers if necessary
    int offset = -1;
    while (offset < 0 && decoder->bytes_left > 0) {
        offset = MP3FindSyncWord(decoder->read_ptr, decoder->bytes_left);
        if (offset >= 0) break;
        
        // Advance buffer, keeping last 3 bytes in case sync word is split
        int advance = decoder->bytes_left > 3 ? decoder->bytes_left - 3 : decoder->bytes_left;
        decoder->bytes_consumed += advance;
        decoder->bytes_left -= advance;
        if (decoder->bytes_left > 0) {
            memmove(decoder->read_buf, decoder->read_ptr + advance, decoder->bytes_left);
        }
        decoder->read_ptr = decoder->read_buf;
        int nread = fread(decoder->read_buf + decoder->bytes_left, 1, MP3_READBUF_SIZE - decoder->bytes_left, f);
        if (nread <= 0 && decoder->bytes_left == 0) break;
        decoder->bytes_left += nread;
    }
    
    if (offset < 0) {
        ESP_LOGE(TAG, "Failed to find MP3 sync word in file %s", path);
        MP3FreeDecoder(decoder->hMP3Decoder);
        free(decoder);
        fclose(f);
        return ESP_FAIL;
    }
    
    decoder->read_ptr += offset;
    decoder->bytes_left -= offset;
    decoder->bytes_consumed += offset;
    
    uint8_t *old_ptr = decoder->read_ptr;
    int16_t dummy_pcm[1152 * 2];
    int err = MP3Decode(decoder->hMP3Decoder, &decoder->read_ptr, &decoder->bytes_left, dummy_pcm, 0);
    
    int decoded_bytes = decoder->read_ptr - old_ptr;
    decoder->bytes_consumed += decoded_bytes;
    
    if (err) {
        ESP_LOGE(TAG, "Failed to decode first frame (err: %d)", err);
        MP3FreeDecoder(decoder->hMP3Decoder);
        free(decoder);
        fclose(f);
        return ESP_FAIL;
    }
    
    MP3FrameInfo frame_info;
    MP3GetLastFrameInfo(decoder->hMP3Decoder, &frame_info);
    
    out_info->sample_rate = frame_info.samprate;
    out_info->num_channels = frame_info.nChans;
    out_info->bits_per_sample = frame_info.bitsPerSample;
    if (out_info->bits_per_sample == 0) {
        out_info->bits_per_sample = 16;
    }
    
    if (frame_info.bitrate > 0) {
        out_info->duration_ms = (uint32_t)(((uint64_t)file_size * 8) / (frame_info.bitrate / 1000));
    } else {
        out_info->duration_ms = 0;
    }
    
    decoder->duration_ms = out_info->duration_ms;
    decoder->sample_rate = out_info->sample_rate;
    
    // Reset to start
    fseek(f, 0, SEEK_SET);
    MP3FreeDecoder(decoder->hMP3Decoder);
    decoder->hMP3Decoder = MP3InitDecoder();
    decoder->bytes_left = 0;
    decoder->read_ptr = decoder->read_buf;
    decoder->bytes_consumed = 0;
    
    *out_handle = decoder;
    return ESP_OK;
}

int mp3_decoder_decode_frame(mp3_handle_t handle, int16_t *pcm_out, int *samples_out) {
    if (!handle || !pcm_out || !samples_out) return -1;
    
    if (handle->bytes_left < MP3_READBUF_SIZE / 2) {
        if (handle->bytes_left > 0 && handle->read_ptr != handle->read_buf) {
            memmove(handle->read_buf, handle->read_ptr, handle->bytes_left);
        }
        handle->read_ptr = handle->read_buf;
        
        int bytes_to_read = MP3_READBUF_SIZE - handle->bytes_left;
        int bytes_read = fread(handle->read_buf + handle->bytes_left, 1, bytes_to_read, handle->file);
        handle->bytes_left += bytes_read;
    }
    
    if (handle->bytes_left == 0) {
        return 0; // EOF
    }
    
    int offset = MP3FindSyncWord(handle->read_ptr, handle->bytes_left);
    if (offset < 0) {
        handle->bytes_consumed += handle->bytes_left;
        handle->bytes_left = 0;
        return -1;
    }
    
    handle->read_ptr += offset;
    handle->bytes_left -= offset;
    handle->bytes_consumed += offset;
    
    uint8_t *old_ptr = handle->read_ptr;
    int err = MP3Decode(handle->hMP3Decoder, &handle->read_ptr, &handle->bytes_left, pcm_out, 0);
    
    int bytes_decoded = handle->read_ptr - old_ptr;
    handle->bytes_consumed += bytes_decoded;
    
    if (err) {
        if (bytes_decoded == 0 && handle->bytes_left > 0) {
            handle->read_ptr++;
            handle->bytes_left--;
            handle->bytes_consumed++;
        }
        return -1;
    }
    
    MP3FrameInfo frame_info;
    MP3GetLastFrameInfo(handle->hMP3Decoder, &frame_info);
    *samples_out = frame_info.outputSamps;
    
    return 1;
}

uint32_t mp3_decoder_get_elapsed_ms(mp3_handle_t handle) {
    if (!handle || handle->file_size == 0) return 0;
    
    double fraction = (double)handle->bytes_consumed / handle->file_size;
    if (fraction > 1.0) fraction = 1.0;
    
    return (uint32_t)(fraction * handle->duration_ms);
}

void mp3_decoder_close(mp3_handle_t handle) {
    if (!handle) return;
    
    if (handle->hMP3Decoder) {
        MP3FreeDecoder(handle->hMP3Decoder);
    }
    
    if (handle->file) {
        fclose(handle->file);
    }
    
    free(handle);
}
