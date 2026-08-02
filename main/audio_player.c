#include "audio_player.h"
#include "config.h"
#include "wav_decoder.h"
#include "mp3_decoder.h"
#include "usb_audio_output.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "AUDIO_PLAYER";

static wav_handle_t s_wav_handle = NULL;
static mp3_handle_t s_mp3_handle = NULL;
static audio_format_t s_current_format = FORMAT_UNKNOWN;
static uint8_t *s_pcm_buf = NULL;

static void close_track(void) {
    if (s_wav_handle) {
        wav_decoder_close(s_wav_handle);
        s_wav_handle = NULL;
    }
    if (s_mp3_handle) {
        mp3_decoder_close(s_mp3_handle);
        s_mp3_handle = NULL;
    }
    s_current_format = FORMAT_UNKNOWN;
}

static void open_track(int index) {
    close_track();

    if (!usb_audio_output_is_connected()) {
        ESP_LOGW(TAG, "Cannot open track: USB-C DAC not connected!");
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_player_state.status = STATUS_ERROR;
        snprintf(g_player_state.error_msg, sizeof(g_player_state.error_msg), "No USB DAC!");
        xSemaphoreGive(g_state_mutex);
        return;
    }

    if (index < 0 || index >= g_track_count) {
        ESP_LOGE(TAG, "Invalid track index: %d", index);
        return;
    }

    track_info_t *track = &g_tracks[index];
    esp_err_t err = ESP_FAIL;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    uint16_t num_channels = 0;
    uint32_t total_ms = 0;

    ESP_LOGI(TAG, "Opening track [%d]: %s", index, track->path);

    if (track->format == FORMAT_WAV) {
        wav_info_t info = {0};
        err = wav_decoder_open(track->path, &s_wav_handle, &info);
        if (err == ESP_OK) {
            s_current_format = FORMAT_WAV;
            sample_rate = info.sample_rate;
            bits_per_sample = info.bits_per_sample;
            num_channels = info.num_channels;
            total_ms = info.duration_ms;
        }
    } else if (track->format == FORMAT_MP3) {
        mp3_info_t info = {0};
        err = mp3_decoder_open(track->path, &s_mp3_handle, &info);
        if (err == ESP_OK) {
            s_current_format = FORMAT_MP3;
            sample_rate = info.sample_rate;
            bits_per_sample = info.bits_per_sample;
            num_channels = info.num_channels;
            total_ms = info.duration_ms;
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open track [%d]: %s", index, track->path);
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_player_state.status = STATUS_ERROR;
        snprintf(g_player_state.error_msg, sizeof(g_player_state.error_msg), "Err: %.50s", track->name);
        xSemaphoreGive(g_state_mutex);
        return;
    }

    usb_audio_output_start_stream(sample_rate, bits_per_sample, num_channels);

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_player_state.track_index = index;
    strncpy(g_player_state.track_name, track->name, MAX_NAME_LEN - 1);
    g_player_state.track_name[MAX_NAME_LEN - 1] = '\0';
    g_player_state.sample_rate = sample_rate;
    g_player_state.bits_per_sample = bits_per_sample;
    g_player_state.num_channels = num_channels;
    g_player_state.total_ms = total_ms;
    g_player_state.elapsed_ms = 0;
    g_player_state.status = STATUS_PLAYING;
    xSemaphoreGive(g_state_mutex);
}

static void track_finished(void) {
    close_track();

    loop_mode_t mode;
    int index;
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    mode = g_player_state.loop_mode;
    index = g_player_state.track_index;
    xSemaphoreGive(g_state_mutex);

    if (mode == LOOP_ONE) {
        open_track(index);
    } else if (mode == LOOP_ALL) {
        open_track((index + 1) % g_track_count);
    } else if (mode == LOOP_SHUFFLE) {
        int next_idx = (g_track_count > 1) ? (rand() % g_track_count) : 0;
        if (next_idx == index && g_track_count > 1) {
            next_idx = (next_idx + 1) % g_track_count;
        }
        open_track(next_idx);
    } else if (mode == LOOP_SEQUENTIAL) {
        if (index < g_track_count - 1) {
            open_track(index + 1);
        } else {
            xSemaphoreTake(g_state_mutex, portMAX_DELAY);
            g_player_state.status = STATUS_STOPPED;
            g_player_state.elapsed_ms = 0;
            xSemaphoreGive(g_state_mutex);
            usb_audio_output_stop();
        }
    }
}

static void handle_command(player_cmd_t cmd) {
    player_status_t status;
    int index;
    uint8_t vol;
    loop_mode_t loop_mode;

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    status = g_player_state.status;
    index = g_player_state.track_index;
    vol = g_player_state.volume;
    loop_mode = g_player_state.loop_mode;
    xSemaphoreGive(g_state_mutex);

    switch (cmd) {
        case CMD_PLAY:
        case CMD_RESUME:
            if (status == STATUS_STOPPED) {
                if (g_track_count > 0) {
                    open_track(index);
                }
            } else if (status == STATUS_PAUSED) {
                usb_audio_output_start_stream(g_player_state.sample_rate, g_player_state.bits_per_sample, g_player_state.num_channels);
                xSemaphoreTake(g_state_mutex, portMAX_DELAY);
                g_player_state.status = STATUS_PLAYING;
                xSemaphoreGive(g_state_mutex);
            }
            break;

        case CMD_PAUSE:
            if (status == STATUS_PLAYING) {
                usb_audio_output_stop();
                xSemaphoreTake(g_state_mutex, portMAX_DELAY);
                g_player_state.status = STATUS_PAUSED;
                xSemaphoreGive(g_state_mutex);
            }
            break;

        case CMD_STOP:
            close_track();
            usb_audio_output_stop();
            xSemaphoreTake(g_state_mutex, portMAX_DELAY);
            g_player_state.status = STATUS_STOPPED;
            g_player_state.elapsed_ms = 0;
            xSemaphoreGive(g_state_mutex);
            break;

        case CMD_NEXT:
            if (g_track_count > 0) {
                int next_idx = (loop_mode == LOOP_SHUFFLE) ? (rand() % g_track_count) : ((index + 1) % g_track_count);
                open_track(next_idx);
            }
            break;

        case CMD_PREV:
            if (g_track_count > 0) {
                int prev_idx = (index - 1 + g_track_count) % g_track_count;
                open_track(prev_idx);
            }
            break;

        case CMD_VOL_UP:
            vol = clamp_int(vol + VOLUME_STEP, VOLUME_MIN, VOLUME_MAX);
            xSemaphoreTake(g_state_mutex, portMAX_DELAY);
            g_player_state.volume = vol;
            xSemaphoreGive(g_state_mutex);
            usb_audio_output_set_volume(vol);
            ESP_LOGI(TAG, "Volume UP: %d%%", vol);
            break;

        case CMD_VOL_DOWN:
            vol = clamp_int(vol - VOLUME_STEP, VOLUME_MIN, VOLUME_MAX);
            xSemaphoreTake(g_state_mutex, portMAX_DELAY);
            g_player_state.volume = vol;
            xSemaphoreGive(g_state_mutex);
            usb_audio_output_set_volume(vol);
            ESP_LOGI(TAG, "Volume DOWN: %d%%", vol);
            break;

        case CMD_LOOP_TOGGLE:
            loop_mode = (loop_mode + 1) % LOOP_MODE_COUNT;
            xSemaphoreTake(g_state_mutex, portMAX_DELAY);
            g_player_state.loop_mode = loop_mode;
            xSemaphoreGive(g_state_mutex);
            break;

        default:
            break;
    }
}

void audio_task(void *arg) {
    ESP_LOGI(TAG, "Audio task started");

    s_pcm_buf = (uint8_t *)malloc(PCM_BUF_SIZE);
    if (!s_pcm_buf) {
        ESP_LOGE(TAG, "Failed to allocate PCM buffer");
        vTaskDelete(NULL);
        return;
    }

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_player_state.status = STATUS_STOPPED;
    xSemaphoreGive(g_state_mutex);

    player_cmd_t cmd = CMD_NONE;

    while (1) {
        player_status_t status;
        uint8_t volume;

        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        status = g_player_state.status;
        volume = g_player_state.volume;
        xSemaphoreGive(g_state_mutex);

        if (status == STATUS_ERROR && usb_audio_output_is_connected()) {
            xSemaphoreTake(g_state_mutex, portMAX_DELAY);
            g_player_state.status = STATUS_STOPPED;
            g_player_state.error_msg[0] = '\0';
            xSemaphoreGive(g_state_mutex);
            status = STATUS_STOPPED;
        }

        if (status == STATUS_STOPPED || status == STATUS_PAUSED || status == STATUS_ERROR || status == STATUS_IDLE || status == STATUS_SCANNING) {
            // Block up to 200ms for commands so we can check for DAC plug/unplug events
            if (xQueueReceive(g_cmd_queue, &cmd, pdMS_TO_TICKS(200)) == pdTRUE) {
                handle_command(cmd);
            }
        } else if (status == STATUS_PLAYING) {
            // Check for commands without blocking
            if (xQueueReceive(g_cmd_queue, &cmd, 0) == pdTRUE) {
                handle_command(cmd);
                continue;
            }

            size_t bytes_written = 0;
            uint32_t elapsed = 0;

            if (s_current_format == FORMAT_WAV && s_wav_handle) {
                int bytes_read = wav_decoder_read(s_wav_handle, s_pcm_buf, PCM_BUF_SIZE);
                if (bytes_read <= 0) {
                    track_finished();
                } else {
                    usb_audio_output_apply_volume((int16_t *)s_pcm_buf, bytes_read / 2, volume);
                    usb_audio_output_write(s_pcm_buf, bytes_read, &bytes_written);
                    elapsed = wav_decoder_get_elapsed_ms(s_wav_handle);
                }
            } else if (s_current_format == FORMAT_MP3 && s_mp3_handle) {
                int samples = 0;
                int res = mp3_decoder_decode_frame(s_mp3_handle, (int16_t *)s_pcm_buf, &samples);
                if (res == 0) {
                    track_finished();
                } else if (res == 1) {
                    usb_audio_output_apply_volume((int16_t *)s_pcm_buf, samples, volume);
                    usb_audio_output_write(s_pcm_buf, samples * sizeof(int16_t), &bytes_written);
                    elapsed = mp3_decoder_get_elapsed_ms(s_mp3_handle);
                } else if (res == -1) {
                    // Decode error, try next frame
                    ESP_LOGW(TAG, "MP3 decode error, skipping frame");
                }
            } else {
                // Invalid state, force stop
                handle_command(CMD_STOP);
            }

            if (status == STATUS_PLAYING) { // might have changed in track_finished
                xSemaphoreTake(g_state_mutex, portMAX_DELAY);
                if (elapsed > 0) {
                    g_player_state.elapsed_ms = elapsed;
                }
                xSemaphoreGive(g_state_mutex);
            }
        }
    }

    if (s_pcm_buf) {
        free(s_pcm_buf);
    }
    vTaskDelete(NULL);
}
