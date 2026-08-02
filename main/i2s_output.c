#include "i2s_output.h"
#include "config.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "I2S_OUT";
static i2s_chan_handle_t tx_chan;

esp_err_t i2s_output_init(uint32_t sample_rate, uint16_t bits_per_sample, uint16_t num_channels) {
    ESP_LOGI(TAG, "Initializing I2S output (SR: %lu, Bits: %u, Ch: %u)", sample_rate, bits_per_sample, num_channels);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = AUDIO_DMA_BUF_COUNT;
    chan_cfg.dma_frame_num = 512;
    chan_cfg.auto_clear = true;
    
    esp_err_t err = i2s_new_channel(&chan_cfg, &tx_chan, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel");
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG((i2s_data_bit_width_t)bits_per_sample, num_channels == 2 ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_I2S_BCK,
            .ws   = PIN_I2S_WS,
            .dout = PIN_I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(tx_chan, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S STD mode");
        return err;
    }

    err = i2s_channel_enable(tx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S channel");
    }
    return err;
}

esp_err_t i2s_output_write(const void *data, size_t len, size_t *bytes_written) {
    if (!tx_chan) return ESP_ERR_INVALID_STATE;
    return i2s_channel_write(tx_chan, data, len, bytes_written, pdMS_TO_TICKS(1000));
}

esp_err_t i2s_output_reconfigure(uint32_t sample_rate, uint16_t bits_per_sample, uint16_t num_channels) {
    if (!tx_chan) return ESP_ERR_INVALID_STATE;
    ESP_LOGI(TAG, "Reconfiguring I2S output (SR: %lu, Bits: %u, Ch: %u)", sample_rate, bits_per_sample, num_channels);
    
    i2s_channel_disable(tx_chan);
    
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    i2s_std_slot_config_t slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG((i2s_data_bit_width_t)bits_per_sample, num_channels == 2 ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO);
    
    esp_err_t err = i2s_channel_reconfig_std_clock(tx_chan, &clk_cfg);
    if (err == ESP_OK) {
        err = i2s_channel_reconfig_std_slot(tx_chan, &slot_cfg);
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure I2S");
        return err;
    }
    
    return i2s_channel_enable(tx_chan);
}

void i2s_output_stop(void) {
    if (tx_chan) {
        i2s_channel_disable(tx_chan);
    }
}

void i2s_output_start(void) {
    if (tx_chan) {
        i2s_channel_enable(tx_chan);
    }
}

void i2s_output_apply_volume(int16_t *pcm_data, int sample_count, uint8_t volume_percent) {
    if (!pcm_data || sample_count <= 0) return;
    
    for (int i = 0; i < sample_count; i++) {
        int32_t val = (int32_t)pcm_data[i] * volume_percent / 100;
        pcm_data[i] = (int16_t)val;
    }
}
