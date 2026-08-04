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

browser_item_t g_browser_items[MAX_BROWSER_ITEMS];
int g_browser_item_count = 0;
ui_state_t g_ui_state = UI_STATE_BOOT;

static char g_current_dir[MAX_PATH_LEN] = SD_MOUNT_POINT;
static int g_browser_selected = 0;
static int g_boot_selected = 0;

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

                switch (g_ui_state) {
                    case UI_STATE_BOOT:
                        if (evt == BTN_EVT_UP_SHORT) {
                            if (g_boot_selected > 0) g_boot_selected--;
                        } else if (evt == BTN_EVT_DOWN_SHORT) {
                            if (g_boot_selected < 0) g_boot_selected++; // Currently 1 item
                        } else if (evt == BTN_EVT_SELECT_SHORT) {
                            if (g_boot_selected == 0) { // 1. Music Player
                                sd_card_list_dir(g_current_dir);
                                g_browser_selected = 0;
                                g_ui_state = UI_STATE_BROWSER;
                            }
                        }
                        break;

                    case UI_STATE_BROWSER:
                        if (evt == BTN_EVT_UP_SHORT) {
                            if (g_browser_selected > 0) g_browser_selected--;
                        } else if (evt == BTN_EVT_DOWN_SHORT) {
                            if (g_browser_selected < g_browser_item_count - 1) g_browser_selected++;
                        } else if (evt == BTN_EVT_SELECT_SHORT) {
                            if (g_browser_item_count > 0 && g_browser_selected < g_browser_item_count) {
                                browser_item_t *item = &g_browser_items[g_browser_selected];
                                if (item->is_dir) {
                                    strncpy(g_current_dir, item->path, MAX_PATH_LEN - 1);
                                    g_current_dir[MAX_PATH_LEN - 1] = '\0';
                                    sd_card_list_dir(g_current_dir);
                                    g_browser_selected = 0;
                                } else {
                                    // Audio file selected!
                                    char clicked_file_path[MAX_PATH_LEN];
                                    strncpy(clicked_file_path, item->path, MAX_PATH_LEN - 1);
                                    clicked_file_path[MAX_PATH_LEN - 1] = '\0';

                                    // Scan current folder (and subfolders) as temporary playlist
                                    sd_card_scan_audio(g_current_dir);

                                    // Find clicked file index in g_tracks
                                    int start_idx = 0;
                                    for (int i = 0; i < g_track_count; i++) {
                                        if (strcmp(g_tracks[i].path, clicked_file_path) == 0) {
                                            start_idx = i;
                                            break;
                                        }
                                    }

                                    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
                                    g_player_state.total_tracks = g_track_count;
                                    g_player_state.track_index = start_idx;
                                    if (g_track_count > 0) {
                                        strncpy(g_player_state.track_name, g_tracks[start_idx].name, MAX_NAME_LEN - 1);
                                        g_player_state.track_name[MAX_NAME_LEN - 1] = '\0';
                                    }
                                    g_player_state.status = STATUS_STOPPED;
                                    xSemaphoreGive(g_state_mutex);

                                    cmd = CMD_STOP;
                                    xQueueSend(g_cmd_queue, &cmd, pdMS_TO_TICKS(50));
                                    cmd = CMD_PLAY;
                                    xQueueSend(g_cmd_queue, &cmd, pdMS_TO_TICKS(50));

                                    g_ui_state = UI_STATE_PLAYER;
                                }
                            }
                        } else if (evt == BTN_EVT_BACK_SHORT || evt == BTN_EVT_BACK_LONG) {
                            // Short or Long press BACK in browser: Navigate up directory / back to main menu
                            if (strcmp(g_current_dir, SD_MOUNT_POINT) != 0) {
                                char *last_slash = strrchr(g_current_dir, '/');
                                if (last_slash && last_slash != g_current_dir) {
                                    *last_slash = '\0';
                                    if (strlen(g_current_dir) < strlen(SD_MOUNT_POINT)) {
                                        strcpy(g_current_dir, SD_MOUNT_POINT);
                                    }
                                } else {
                                    strcpy(g_current_dir, SD_MOUNT_POINT);
                                }
                                sd_card_list_dir(g_current_dir);
                                g_browser_selected = 0;
                            } else {
                                g_ui_state = UI_STATE_BOOT;
                            }
                        }
                        break;

                    case UI_STATE_PLAYER:
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
                            case BTN_EVT_BACK_LONG:
                                // Long press BACK in player returns to browser!
                                sd_card_list_dir(g_current_dir);
                                g_ui_state = UI_STATE_BROWSER;
                                break;
                            default: break;
                        }
                        break;
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
            switch (g_ui_state) {
                case UI_STATE_BOOT:
                    oled_draw_boot_screen(g_boot_selected);
                    break;
                case UI_STATE_BROWSER:
                    oled_draw_browser(g_current_dir, g_browser_selected);
                    break;
                case UI_STATE_PLAYER:
                    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
                    memcpy(&local_state, &g_player_state, sizeof(player_state_t));
                    xSemaphoreGive(g_state_mutex);
                    oled_update(&local_state);
                    break;
            }
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
    
    // 2. Init OLED
    oled_init();
    
    // 3. Create sync primitives
    g_cmd_queue = xQueueCreate(16, sizeof(player_cmd_t));
    g_state_mutex = xSemaphoreCreateMutex();
    g_player_state.volume = VOLUME_DEFAULT;
    g_player_state.loop_mode = LOOP_ALL;
    g_player_state.status = STATUS_IDLE;
    
    // 4. Mount SD card
    oled_show_message("SD Sound Player", "Mounting SD...");
    if (sd_card_init() != ESP_OK) {
        oled_show_message("ERROR", "SD Card Failed!");
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_player_state.status = STATUS_ERROR;
        snprintf(g_player_state.error_msg, sizeof(g_player_state.error_msg), "SD mount failed");
        xSemaphoreGive(g_state_mutex);
    } else {
        // SD card mounted successfully! Show boot screen.
        g_ui_state = UI_STATE_BOOT;
        oled_draw_boot_screen(g_boot_selected);
    }

    // 5. Init USB Audio Output (USB Host UAC)
    usb_audio_output_init();
    vTaskDelay(pdMS_TO_TICKS(200));

    // 6. Create tasks
    xTaskCreatePinnedToCore(audio_task, "audio_task", 8192, NULL, 5, NULL, 1);  // Core 1, high priority
    xTaskCreatePinnedToCore(ui_task, "ui_task", 4096, NULL, 3, NULL, 0);        // Core 0, normal priority
    
    ESP_LOGI(TAG, "Sound Player started. Ready in boot screen.");
}
