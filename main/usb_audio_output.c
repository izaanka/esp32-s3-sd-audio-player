#include "usb_audio_output.h"
#include "config.h"
#include "usb/usb_host.h"
#include "usb/uac_host.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <wchar.h>
#include <stdlib.h>

static const char *TAG = "USB_AUDIO_OUT";

static uac_host_device_handle_t s_uac_handle = NULL;
static bool s_uac_connected = false;
static bool s_stream_running = false;
static uint32_t s_curr_sample_rate = 0;
static uint16_t s_curr_bits = 0;
static uint16_t s_curr_channels = 0;
static SemaphoreHandle_t s_uac_mutex = NULL;
static uint8_t s_current_volume = VOLUME_DEFAULT;

static void uac_device_event_cb(uac_host_device_handle_t uac_device_handle,
                                const uac_host_device_event_t event, void *arg) {
    if (event == UAC_HOST_DRIVER_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "USB-C DAC disconnected!");
        xSemaphoreTake(s_uac_mutex, portMAX_DELAY);
        s_uac_connected = false;
        s_stream_running = false;
        s_uac_handle = NULL;
        xSemaphoreGive(s_uac_mutex);
    }
}

typedef struct {
    uint8_t addr;
    uint8_t iface_num;
    uac_host_driver_event_t event;
} usb_evt_t;

static QueueHandle_t s_usb_evt_queue = NULL;

static void uac_event_task(void *arg) {
    usb_evt_t evt;
    while (1) {
        if (xQueueReceive(s_usb_evt_queue, &evt, portMAX_DELAY) == pdTRUE) {
            if (evt.event == UAC_HOST_DRIVER_EVENT_TX_CONNECTED) {
                ESP_LOGI(TAG, "Opening USB UAC TX device at addr %d, iface %d...", evt.addr, evt.iface_num);
                uac_host_device_config_t dev_config = {
                    .addr = evt.addr,
                    .iface_num = evt.iface_num,
                    .buffer_size = 32768,
                    .buffer_threshold = 4096,
                    .callback = uac_device_event_cb,
                    .callback_arg = NULL,
                };
                uac_host_device_handle_t dev_handle = NULL;
                esp_err_t ret = uac_host_device_open(&dev_config, &dev_handle);
                if (ret == ESP_OK) {
                    uac_host_dev_info_t dev_info;
                    if (uac_host_get_device_info(dev_handle, &dev_info) == ESP_OK) {
                        ESP_LOGI(TAG, "USB DAC VID: 0x%04X, PID: 0x%04X", dev_info.VID, dev_info.PID);
                        char mfg[64] = {0}, prod[64] = {0};
                        wcstombs(mfg, dev_info.iManufacturer, sizeof(mfg) - 1);
                        wcstombs(prod, dev_info.iProduct, sizeof(prod) - 1);
                        ESP_LOGI(TAG, "USB DAC Manufacturer: '%s', Product: '%s'", mfg[0] ? mfg : "Unknown", prod[0] ? prod : "Audio Device");
                    }

                    uac_host_dev_alt_param_t alt_param;
                    if (uac_host_get_device_alt_param(dev_handle, 1, &alt_param) == ESP_OK) {
                        uac_host_stream_config_t stm_config = {
                            .channels = alt_param.channels,
                            .bit_resolution = alt_param.bit_resolution,
                            .sample_freq = alt_param.sample_freq_type == 0 ? 44100 : alt_param.sample_freq[0],
                        };
                        if (uac_host_device_start(dev_handle, &stm_config) == ESP_OK) {
                            ESP_LOGI(TAG, "UAC Stream started (%lu Hz, %d channels)", stm_config.sample_freq, stm_config.channels);
                        }
                    }

                    // Unmute and set hardware volume to default (50%)
                    uac_host_device_set_mute(dev_handle, false);
                    uac_host_device_set_volume(dev_handle, s_current_volume);

                    xSemaphoreTake(s_uac_mutex, portMAX_DELAY);
                    s_uac_handle = dev_handle;
                    s_uac_connected = true;
                    s_stream_running = true;
                    xSemaphoreGive(s_uac_mutex);
                    ESP_LOGI(TAG, "USB-C DAC OPENED & READY!");
                    uac_host_printf_device_param(dev_handle);
                } else {
                    ESP_LOGE(TAG, "Failed to open USB-C DAC device: 0x%x", ret);
                }
            }
        }
    }
}

static void uac_driver_event_cb(uint8_t addr, uint8_t iface_num,
                                const uac_host_driver_event_t event, void *arg) {
    if (event == UAC_HOST_DRIVER_EVENT_TX_CONNECTED) {
        ESP_LOGI(TAG, "USB UAC TX (Speaker/DAC) device detected at addr %d, iface %d", addr, iface_num);
        usb_evt_t evt = { .addr = addr, .iface_num = iface_num, .event = event };
        xQueueSend(s_usb_evt_queue, &evt, 0);
    }
}

static void usb_host_task(void *arg) {
    ESP_LOGI(TAG, "USB Host Library task started");
    while (1) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI(TAG, "USB Host Library: No clients");
            usb_host_device_free_all();
        }
    }
}

esp_err_t usb_audio_output_init(void) {
    if (s_uac_mutex == NULL) {
        s_uac_mutex = xSemaphoreCreateMutex();
    }

    // Enable VBUS power switch on boards that use GPIO for VBUS power control (GPIO 18, 38)
    gpio_config_t pwr_conf = {
        .pin_bit_mask = (1ULL << 18) | (1ULL << 38),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pwr_conf);
    gpio_set_level(18, 1);
    gpio_set_level(38, 1);

    // 1. Install USB Host Library
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL2,
    };
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install USB Host stack: %d", err);
        return err;
    }

    // Task for handling base USB Host Library events
    xTaskCreatePinnedToCore(usb_host_task, "usb_host", 4096, NULL, 2, NULL, 0);

    // 2. Install UAC Host Class Driver with background event task
    uac_host_driver_config_t driver_config = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = uac_driver_event_cb,
        .callback_arg = NULL,
    };

    if (s_usb_evt_queue == NULL) {
        s_usb_evt_queue = xQueueCreate(10, sizeof(usb_evt_t));
        xTaskCreate(uac_event_task, "uac_evt", 4096, NULL, 4, NULL);
    }

    err = uac_host_install(&driver_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UAC Host driver: %d", err);
        return err;
    }

    ESP_LOGI(TAG, "USB Host UAC Driver initialized. Waiting for USB-C DAC connection on GPIO 19 (D-) / GPIO 20 (D+)...");
    return ESP_OK;
}

bool usb_audio_output_is_connected(void) {
    bool conn = false;
    if (s_uac_mutex) {
        xSemaphoreTake(s_uac_mutex, portMAX_DELAY);
        conn = s_uac_connected;
        xSemaphoreGive(s_uac_mutex);
    }
    return conn;
}

static uint32_t s_dac_native_freq = 48000;
static int16_t s_resample_buf[PCM_BUF_SIZE * 2];

static int resample_pcm16_stereo(const int16_t *in_pcm, int in_samples,
                                int16_t *out_pcm, uint32_t in_freq, uint32_t out_freq) {
    if (in_freq == out_freq || in_freq == 0 || out_freq == 0) {
        memcpy(out_pcm, in_pcm, in_samples * sizeof(int16_t));
        return in_samples;
    }
    int in_frames = in_samples / 2;
    int out_frames = (int)((uint64_t)in_frames * out_freq / in_freq);
    if (out_frames > PCM_BUF_SIZE) out_frames = PCM_BUF_SIZE;

    for (int i = 0; i < out_frames; i++) {
        uint32_t src_pos_q16 = (uint32_t)(((uint64_t)i * in_freq << 16) / out_freq);
        int idx = src_pos_q16 >> 16;
        uint32_t frac = src_pos_q16 & 0xFFFF;
        if (idx >= in_frames - 1) {
            out_pcm[i * 2]     = in_pcm[(in_frames - 1) * 2];
            out_pcm[i * 2 + 1] = in_pcm[(in_frames - 1) * 2 + 1];
        } else {
            int16_t l0 = in_pcm[idx * 2];
            int16_t l1 = in_pcm[(idx + 1) * 2];
            int16_t r0 = in_pcm[idx * 2 + 1];
            int16_t r1 = in_pcm[(idx + 1) * 2 + 1];
            out_pcm[i * 2]     = (int16_t)(l0 + (((int32_t)(l1 - l0) * frac) >> 16));
            out_pcm[i * 2 + 1] = (int16_t)(r0 + (((int32_t)(r1 - r0) * frac) >> 16));
        }
    }
    return out_frames * 2;
}

esp_err_t usb_audio_output_start_stream(uint32_t sample_rate, uint16_t bits_per_sample, uint16_t num_channels) {
    xSemaphoreTake(s_uac_mutex, portMAX_DELAY);
    if (!s_uac_connected || !s_uac_handle) {
        xSemaphoreGive(s_uac_mutex);
        ESP_LOGW(TAG, "Cannot start stream: USB-C DAC not connected");
        return ESP_ERR_INVALID_STATE;
    }

    // Try starting with requested sample rate first
    uac_host_stream_config_t stream_config = {
        .channels = (uint8_t)num_channels,
        .bit_resolution = (uint8_t)bits_per_sample,
        .sample_freq = sample_rate,
        .flags = 0,
    };

    esp_err_t err = uac_host_device_start(s_uac_handle, &stream_config);
    if (err != ESP_OK) {
        // Fallback to DAC's native sample frequency (48000 Hz)
        ESP_LOGW(TAG, "Sample rate %lu Hz rejected (0x%x), falling back to DAC native %lu Hz", sample_rate, err, s_dac_native_freq);
        stream_config.sample_freq = s_dac_native_freq;
        err = uac_host_device_start(s_uac_handle, &stream_config);
    }

    if (err == ESP_OK) {
        s_stream_running = true;
        s_curr_sample_rate = sample_rate; // Input stream rate
        s_curr_bits = bits_per_sample;
        s_curr_channels = num_channels;
        ESP_LOGI(TAG, "USB UAC stream running (Input: %lu Hz -> USB DAC: %lu Hz)", sample_rate, stream_config.sample_freq);
    } else {
        ESP_LOGE(TAG, "Failed to start UAC stream at native rate (err 0x%x)", err);
    }

    xSemaphoreGive(s_uac_mutex);
    return err;
}

esp_err_t usb_audio_output_reconfigure(uint32_t sample_rate, uint16_t bits_per_sample, uint16_t num_channels) {
    if (s_stream_running && s_curr_sample_rate == sample_rate &&
        s_curr_bits == bits_per_sample && s_curr_channels == num_channels) {
        return ESP_OK;
    }
    return usb_audio_output_start_stream(sample_rate, bits_per_sample, num_channels);
}

esp_err_t usb_audio_output_write(const void *data, size_t len, size_t *bytes_written) {
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;
    if (bytes_written) *bytes_written = 0;

    xSemaphoreTake(s_uac_mutex, portMAX_DELAY);
    if (!s_uac_connected || !s_uac_handle || !s_stream_running) {
        xSemaphoreGive(s_uac_mutex);
        vTaskDelay(pdMS_TO_TICKS(10)); // Prevent tight loop run-away if disconnected
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t *write_data = (const uint8_t*)data;
    uint32_t write_len = (uint32_t)len;

    // If input sample rate differs from DAC native 48000Hz, resample PCM
    if (s_curr_sample_rate > 0 && s_curr_sample_rate != s_dac_native_freq) {
        int in_samples = len / sizeof(int16_t);
        int out_samples = resample_pcm16_stereo((const int16_t*)data, in_samples,
                                                s_resample_buf, s_curr_sample_rate, s_dac_native_freq);
        write_data = (const uint8_t*)s_resample_buf;
        write_len = (uint32_t)(out_samples * sizeof(int16_t));
    }

    esp_err_t err = uac_host_device_write(s_uac_handle, (uint8_t*)write_data, write_len, pdMS_TO_TICKS(200));
    if (err == ESP_OK && bytes_written) {
        *bytes_written = len;
    } else if (err != ESP_OK) {
        // If ring buffer full, yield to allow USB Host ISO task to drain
        vTaskDelay(pdMS_TO_TICKS(10));
        if (bytes_written) *bytes_written = len; // count as consumed to maintain pacing
    }

    xSemaphoreGive(s_uac_mutex);
    return ESP_OK;
}

void usb_audio_output_stop(void) {
    xSemaphoreTake(s_uac_mutex, portMAX_DELAY);
    if (s_uac_connected && s_uac_handle && s_stream_running) {
        uac_host_device_stop(s_uac_handle);
        s_stream_running = false;
        ESP_LOGI(TAG, "USB UAC stream stopped");
    }
    xSemaphoreGive(s_uac_mutex);
}

void usb_audio_output_apply_volume(int16_t *pcm_data, int sample_count, uint8_t volume_percent) {
    if (!pcm_data || sample_count <= 0) return;
    if (volume_percent > 100) volume_percent = 100;
    if (volume_percent == 100) return;

    for (int i = 0; i < sample_count; i++) {
        pcm_data[i] = (int16_t)(((int32_t)pcm_data[i] * volume_percent) / 100);
    }
}

esp_err_t usb_audio_output_set_volume(uint8_t volume_percent) {
    if (volume_percent > 100) volume_percent = 100;
    s_current_volume = volume_percent;
    xSemaphoreTake(s_uac_mutex, portMAX_DELAY);
    if (s_uac_connected && s_uac_handle) {
        uac_host_device_set_volume(s_uac_handle, volume_percent);
    }
    xSemaphoreGive(s_uac_mutex);
    return ESP_OK;
}
