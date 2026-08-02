#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "config.h"
#include "sd_card.h"
#include "usb_audio_output.h"
#include "button_handler.h"
#include "oled_display.h"
#include "audio_player.h"

static const char *TAG = "MAIN";

// Define all the global variables declared as extern in config.h:
QueueHandle_t g_cmd_queue = NULL;
SemaphoreHandle_t g_state_mutex = NULL;
player_state_t g_player_state = {0};
track_info_t g_tracks[MAX_TRACKS];
int g_track_count = 0;

#define OLED_SLEEP_TIMEOUT_MS 15000 // 15 seconds auto-sleep timeout to prevent OLED burn-in

// UI task function:
void ui_task(void *arg) {
    button_init();
    player_state_t local_state;
    TickType_t last_oled_update = 0;
    TickType_t last_activity_time = xTaskGetTickCount();
    
    while (1) {
        // Poll buttons
        button_event_t evt = button_poll();
        if (evt != BTN_EVT_NONE) {
            last_activity_time = xTaskGetTickCount();
            if (oled_is_sleeping()) {
                // Wake up screen on first button press
                oled_set_sleep(false);
            } else {
                player_cmd_t cmd = CMD_NONE;
                player_status_t st;
                switch (evt) {
                    case BTN_EVT_SELECT_SHORT:
                        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
                        st = g_player_state.status;
                        xSemaphoreGive(g_state_mutex);
                        if (st == STATUS_PLAYING) cmd = CMD_PAUSE;
                        else if (st == STATUS_PAUSED) cmd = CMD_RESUME;
                        else if (st == STATUS_STOPPED) cmd = CMD_PLAY;
                        break;
                    case BTN_EVT_UP_SHORT:   cmd = CMD_VOL_UP;       break;
                    case BTN_EVT_UP_LONG:    cmd = CMD_NEXT;          break;
                    case BTN_EVT_DOWN_SHORT: cmd = CMD_VOL_DOWN;      break;
                    case BTN_EVT_DOWN_LONG:  cmd = CMD_PREV;          break;
                    case BTN_EVT_BACK_SHORT: cmd = CMD_LOOP_TOGGLE;   break;
                    default: break;
                }
                if (cmd != CMD_NONE) {
                    xQueueSend(g_cmd_queue, &cmd, pdMS_TO_TICKS(50));
                }
            }
        }
        
        // Auto-sleep check after 15 seconds of inactivity
        if (!oled_is_sleeping()) {
            if ((xTaskGetTickCount() - last_activity_time) >= pdMS_TO_TICKS(OLED_SLEEP_TIMEOUT_MS)) {
                oled_set_sleep(true);
            }
        }
        
        // Update OLED at OLED_REFRESH_INTERVAL_MS (only if awake)
        if (!oled_is_sleeping() && ((xTaskGetTickCount() - last_oled_update) >= pdMS_TO_TICKS(OLED_REFRESH_INTERVAL_MS))) {
            xSemaphoreTake(g_state_mutex, portMAX_DELAY);
            memcpy(&local_state, &g_player_state, sizeof(player_state_t));
            xSemaphoreGive(g_state_mutex);
            oled_update(&local_state);
            last_oled_update = xTaskGetTickCount();
        }
        
        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_INTERVAL_MS));
    }
}

void app_main(void) {
    // 1. Init NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    
    // 1. Init OLED (show startup message)
    oled_init();
    oled_show_message("SD Sound Player", "Initializing...");
    
    // 2. Create sync primitives
    g_cmd_queue = xQueueCreate(16, sizeof(player_cmd_t));
    g_state_mutex = xSemaphoreCreateMutex();
    g_player_state.volume = VOLUME_DEFAULT;
    g_player_state.loop_mode = LOOP_ALL;
    g_player_state.status = STATUS_IDLE;
    
    // 3. Mount SD card
    oled_show_message("SD Sound Player", "Mounting SD...");
    if (sd_card_init() != ESP_OK) {
        oled_show_message("ERROR", "SD Card Failed!");
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_player_state.status = STATUS_ERROR;
        snprintf(g_player_state.error_msg, sizeof(g_player_state.error_msg), "SD mount failed");
        xSemaphoreGive(g_state_mutex);
    } else {
        // 4. Scan for audio files
        oled_show_message("SD Sound Player", "Scanning files...");
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_player_state.status = STATUS_SCANNING;
        xSemaphoreGive(g_state_mutex);
        
        g_track_count = sd_card_scan_audio();
        
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_player_state.total_tracks = g_track_count;
        if (g_track_count > 0) {
            g_player_state.status = STATUS_STOPPED;
            g_player_state.track_index = 0;
            strncpy(g_player_state.track_name, g_tracks[0].name, MAX_NAME_LEN - 1);
            g_player_state.track_name[MAX_NAME_LEN - 1] = '\0';
        } else {
            g_player_state.status = STATUS_ERROR;
            snprintf(g_player_state.error_msg, sizeof(g_player_state.error_msg), "No audio files found");
        }
        xSemaphoreGive(g_state_mutex);
        
        char msg[32];
        snprintf(msg, sizeof(msg), "Found %d tracks", g_track_count);
        oled_show_message("SD Sound Player", msg);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // 5. Init USB Audio Output (USB Host UAC)
    usb_audio_output_init();
    vTaskDelay(pdMS_TO_TICKS(200));

    // 6. Create tasks
    xTaskCreatePinnedToCore(audio_task, "audio_task", 8192, NULL, 5, NULL, 1);  // Core 1, high priority
    xTaskCreatePinnedToCore(ui_task, "ui_task", 4096, NULL, 3, NULL, 0);        // Core 0, normal priority
    
    ESP_LOGI(TAG, "Sound Player started. %d tracks loaded.", g_track_count);
}
