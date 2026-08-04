#include "oled_display.h"
#include "font5x7.h"
#include "usb_audio_output.h"
#include "ereader.h"
#include "driver/i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "OLED";
static esp_lcd_panel_handle_t panel_handle = NULL;
static uint8_t framebuf[OLED_WIDTH * OLED_HEIGHT / 8];

static void fb_clear(void) {
    memset(framebuf, 0, sizeof(framebuf));
}

static void fb_set_pixel(int x, int y, bool on) {
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) return;
    int page = y / 8;
    int bit = y % 8;
    if (on) {
        framebuf[page * OLED_WIDTH + x] |= (1 << bit);
    } else {
        framebuf[page * OLED_WIDTH + x] &= ~(1 << bit);
    }
}

static void fb_draw_char(int x, int y, char ch) {
    if (ch < FONT_FIRST || ch > FONT_LAST) ch = ' ';
    const uint8_t *bitmap = font5x7[ch - FONT_FIRST];
    for (int col = 0; col < FONT_WIDTH; col++) {
        for (int row = 0; row < FONT_HEIGHT; row++) {
            if (bitmap[col] & (1 << row)) {
                fb_set_pixel(x + col, y + row, true);
            }
        }
    }
}

static void fb_draw_string(int x, int y, const char *str) {
    while (*str) {
        fb_draw_char(x, y, *str);
        x += FONT_CHAR_W;
        str++;
    }
}

static void fb_draw_hline(int x, int y, int w) {
    for (int i = 0; i < w; i++) {
        fb_set_pixel(x + i, y, true);
    }
}

static void fb_draw_rect_filled(int x, int y, int w, int h) {
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            fb_set_pixel(x + i, y + j, true);
        }
    }
}

static void fb_flush(void) {
    if (panel_handle) {
        esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, OLED_WIDTH, OLED_HEIGHT, framebuf);
    }
}

esp_err_t oled_init(void) {
    ESP_LOGI(TAG, "Initializing OLED...");
    esp_err_t err = ESP_OK;

    struct { int sda; int scl; } pin_pairs[] = {
        {PIN_OLED_SDA, PIN_OLED_SCL}, // 42, 41
        {8, 9},                       // Default ESP32-S3 I2C
        {1, 2},                       // Alternate I2C
        {4, 5},
    };
    int num_pairs = sizeof(pin_pairs) / sizeof(pin_pairs[0]);
    
    uint8_t detected_addr = OLED_I2C_ADDR;
    bool found_dev = false;

    for (int p = 0; p < num_pairs && !found_dev; p++) {
        int sda = pin_pairs[p].sda;
        int scl = pin_pairs[p].scl;

        i2c_config_t i2c_conf = {
            .mode = I2C_MODE_MASTER,
            .sda_io_num = sda,
            .scl_io_num = scl,
            .sda_pullup_en = GPIO_PULLUP_ENABLE,
            .scl_pullup_en = GPIO_PULLUP_ENABLE,
            .master.clk_speed = 100000,
        };
        i2c_param_config(I2C_NUM_0, &i2c_conf);
        i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);

        for (uint8_t addr = 1; addr < 127; addr++) {
            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
            i2c_master_stop(cmd);
            esp_err_t scan_err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(30));
            i2c_cmd_link_delete(cmd);
            if (scan_err == ESP_OK) {
                ESP_LOGI(TAG, "I2C device found at 0x%02X on SDA: %d, SCL: %d", addr, sda, scl);
                if (addr == 0x3C || addr == 0x3D) {
                    detected_addr = addr;
                    found_dev = true;
                    break;
                }
            }
        }
        if (!found_dev) {
            i2c_driver_delete(I2C_NUM_0);
        }
    }
    if (!found_dev) {
        ESP_LOGE(TAG, "No OLED display detected on I2C bus. Skipping display updates.");
        panel_handle = NULL;
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "Using OLED at address 0x%02X", detected_addr);

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = detected_addr,
        .control_phase_bytes = 1,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .dc_bit_offset = 6,
    };
    err = esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_NUM_0, &io_config, &io_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create panel IO");
        panel_handle = NULL;
        return err;
    }

    esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,
    };
    err = esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create SSD1306 panel");
        panel_handle = NULL;
        return err;
    }

    err = esp_lcd_panel_reset(panel_handle);
    if (err != ESP_OK) ESP_LOGW(TAG, "Panel reset warning: %d", err);

    err = esp_lcd_panel_init(panel_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init SSD1306 panel: %d", err);
        panel_handle = NULL;
        return err;
    }

    esp_lcd_panel_disp_on_off(panel_handle, true);

    fb_clear();
    fb_flush();
    return ESP_OK;
}

static bool s_is_sleeping = false;

void oled_set_sleep(bool sleep) {
    if (!panel_handle) return;
    if (s_is_sleeping == sleep) return;
    s_is_sleeping = sleep;
    esp_lcd_panel_disp_on_off(panel_handle, !sleep);
}

bool oled_is_sleeping(void) {
    return s_is_sleeping;
}

static uint32_t scroll_offset = 0;
static uint32_t last_scroll_time = 0;

void oled_update(const player_state_t *state) {
    if (s_is_sleeping) return;
    fb_clear();
    char buf[64];

    // Line 0
    snprintf(buf, sizeof(buf), "# SD Player    V:%d%%", state->volume);
    fb_draw_string(0, 0, buf);

    // Line 1
    fb_draw_hline(0, 8, 128);

    // Line 2
    snprintf(buf, sizeof(buf), "Track %2d / %d", state->track_index + 1, state->total_tracks);
    fb_draw_string(0, 16, buf);

    // Line 3 - Filename
    int name_len = strlen(state->track_name);
    if (name_len > OLED_SCROLL_CHARS_VISIBLE) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if (now - last_scroll_time > OLED_SCROLL_INTERVAL_MS) {
            scroll_offset = (scroll_offset + 1) % (name_len + 4);
            last_scroll_time = now;
        }
        
        char scroll_buf[OLED_SCROLL_CHARS_VISIBLE + 1];
        for(int i = 0; i < OLED_SCROLL_CHARS_VISIBLE; i++) {
            int idx = (scroll_offset + i) % (name_len + 4);
            if (idx < name_len) {
                scroll_buf[i] = state->track_name[idx];
            } else {
                scroll_buf[i] = ' '; // padding between repeats
            }
        }
        scroll_buf[OLED_SCROLL_CHARS_VISIBLE] = '\0';
        fb_draw_string(0, 24, scroll_buf);
    } else {
        fb_draw_string(0, 24, state->track_name);
        scroll_offset = 0;
    }

    // Line 4 - Audio info
    snprintf(buf, sizeof(buf), "%luHz %db %s", state->sample_rate, state->bits_per_sample, state->num_channels == 2 ? "Stereo" : "Mono");
    fb_draw_string(0, 32, buf);

    // Line 5 - Time and Status
    if (state->status == STATUS_STOPPED) {
        fb_draw_string(0, 40, "Press SELECT to play");
    } else if (state->status == STATUS_ERROR) {
        fb_draw_string(0, 40, state->error_msg);
    } else if (state->status == STATUS_SCANNING) {
        fb_draw_string(0, 40, "Scanning SD card...");
    } else {
        const char *icon = "[]";
        if (state->status == STATUS_PLAYING) icon = ">";
        else if (state->status == STATUS_PAUSED) icon = "||";
        
        int el_min = (state->elapsed_ms / 1000) / 60;
        int el_sec = (state->elapsed_ms / 1000) % 60;
        int tot_min = (state->total_ms / 1000) / 60;
        int tot_sec = (state->total_ms / 1000) % 60;
        
        snprintf(buf, sizeof(buf), "%s %02d:%02d / %02d:%02d", icon, el_min, el_sec, tot_min, tot_sec);
        fb_draw_string(0, 40, buf);
    }

    // Line 6 - Progress bar
    fb_draw_string(0, 48, "[                  ]"); // 20 chars
    if (state->total_ms > 0 && (state->status == STATUS_PLAYING || state->status == STATUS_PAUSED)) {
        int bar_width = 18 * FONT_CHAR_W; // Width inside brackets
        int filled = (int)((uint64_t)state->elapsed_ms * bar_width / state->total_ms);
        if (filled > bar_width) filled = bar_width;
        fb_draw_rect_filled(FONT_CHAR_W, 48, filled, 7);
    }

    // Line 7 - Loop mode & USB DAC status
    snprintf(buf, sizeof(buf), "Lp:%s | %s", loop_mode_str(state->loop_mode), usb_audio_output_is_connected() ? "DAC:OK" : "NO DAC");
    fb_draw_string(0, 56, buf);

    fb_flush();
}

void oled_draw_boot_screen(int selected_index) {
    fb_clear();
    
    // Header
    fb_draw_string(10, 2, "ESP32-S3 Explorer");
    fb_draw_hline(0, 11, 128);
    
    // Menu List
    static const char *menu_items[] = {
        "1. Music Player",
        "2. Player",
        "3. E-Reader"
    };
    int num_items = sizeof(menu_items) / sizeof(menu_items[0]);
    
    for (int i = 0; i < num_items; i++) {
        char buf[32];
        bool is_sel = (i == selected_index);
        snprintf(buf, sizeof(buf), "%c %s", is_sel ? '>' : ' ', menu_items[i]);
        fb_draw_string(4, 18 + (i * 12), buf);
    }
    
    // Footer
    fb_draw_hline(0, 56, 128);
    fb_draw_string(16, 57, "SELECT: Open Menu");
    
    fb_flush();
}

void oled_draw_ereader_page(void) {
    fb_clear();
    
    char lines[6][22];
    ereader_get_page_lines(lines);
    
    for (int i = 0; i < 6; i++) {
        fb_draw_string(0, i * 9, lines[i]);
    }
    
    fb_draw_hline(0, 55, 128);
    
    const ereader_info_t *info = ereader_get_info();
    char footer[64];
    const char *auto_str = "Off";
    if (info->auto_scroll_sec == 5) auto_str = "5s";
    else if (info->auto_scroll_sec == 10) auto_str = "10s";
    else if (info->auto_scroll_sec == 15) auto_str = "15s";
    
    snprintf(footer, sizeof(footer), "Pg %d/%d|A:%s|B:%d", 
             info->current_page + 1, 
             info->total_pages > 0 ? info->total_pages : 1, 
             auto_str, 
             info->bookmark_count);
    footer[21] = '\0';
    fb_draw_string(0, 56, footer);
    
    fb_flush();
}

void oled_draw_ereader_menu(int menu_index) {
    fb_clear();
    
    fb_draw_string(24, 2, "E-Reader Menu");
    fb_draw_hline(0, 11, 128);
    
    const ereader_info_t *info = ereader_get_info();
    char auto_buf[32];
    snprintf(auto_buf, sizeof(auto_buf), "1. AutoScroll (%ds)", info->auto_scroll_sec);
    
    const char *menu_items[] = {
        auto_buf,
        "2. Add Bookmark",
        "3. View Bookmarks",
        "4. Exit Reader"
    };
    int num_items = sizeof(menu_items) / sizeof(menu_items[0]);
    
    for (int i = 0; i < num_items; i++) {
        char buf[32];
        bool is_sel = (i == menu_index);
        snprintf(buf, sizeof(buf), "%c %s", is_sel ? '>' : ' ', menu_items[i]);
        fb_draw_string(0, 16 + (i * 10), buf);
    }
    
    fb_draw_hline(0, 56, 128);
    fb_draw_string(20, 57, "SELECT: Apply");
    
    fb_flush();
}

void oled_draw_autoscroll_menu(int selected_index) {
    fb_clear();
    
    fb_draw_string(14, 2, "AutoScroll Speed");
    fb_draw_hline(0, 11, 128);
    
    static const int autoscroll_values[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 20, 25, 30};
    int total_options = sizeof(autoscroll_values) / sizeof(autoscroll_values[0]);
    
    int top = selected_index - 1;
    if (top < 0) top = 0;
    if (top + 4 > total_options) top = total_options - 4;
    if (top < 0) top = 0;
    
    for (int i = 0; i < 4; i++) {
        int idx = top + i;
        if (idx >= total_options) break;
        
        int sec = autoscroll_values[idx];
        bool is_sel = (idx == selected_index);
        
        char label[32];
        if (sec == 0) {
            snprintf(label, sizeof(label), "%c Off", is_sel ? '>' : ' ');
        } else {
            snprintf(label, sizeof(label), "%c %d seconds", is_sel ? '>' : ' ', sec);
        }
        
        fb_draw_string(4, 14 + (i * 10), label);
    }
    
    fb_draw_hline(0, 56, 128);
    char footer[32];
    snprintf(footer, sizeof(footer), "%d/%d | SELECT:Set", selected_index + 1, total_options);
    fb_draw_string(0, 57, footer);
    
    fb_flush();
}

void oled_draw_bookmark_menu(int selected_index) {
    fb_clear();
    
    fb_draw_string(18, 2, "Saved Bookmarks");
    fb_draw_hline(0, 11, 128);
    
    int count = ereader_get_bookmark_count();
    if (count == 0) {
        fb_draw_string(12, 28, "No Bookmarks Yet!");
    } else {
        int top = selected_index - 1;
        if (top < 0) top = 0;
        if (top + 4 > count) top = count - 4;
        if (top < 0) top = 0;
        
        for (int i = 0; i < 4; i++) {
            int idx = top + i;
            if (idx >= count) break;
            
            const ereader_bookmark_t *bm = ereader_get_bookmark(idx);
            bool is_sel = (idx == selected_index);
            
            char label[64];
            snprintf(label, sizeof(label), "%c %d) \"%s\"", 
                     is_sel ? '>' : ' ', 
                     idx + 1, 
                     bm ? bm->snippet : "");
            label[21] = '\0';
            fb_draw_string(0, 14 + (i * 10), label);
        }
    }
    
    fb_draw_hline(0, 56, 128);
    char footer[64];
    if (count > 0) {
        snprintf(footer, sizeof(footer), "%d/%d | SELECT:Jump", selected_index + 1, count);
    } else {
        snprintf(footer, sizeof(footer), "BACK: Return");
    }
    fb_draw_string(0, 57, footer);
    
    fb_flush();
}

void oled_draw_browser(const char *current_path, int selected_index) {
    fb_clear();
    
    // Header line - path
    char path_buf[22];
    int path_len = strlen(current_path);
    if (path_len > 21) {
        snprintf(path_buf, sizeof(path_buf), "...%s", current_path + (path_len - 18));
    } else {
        snprintf(path_buf, sizeof(path_buf), "%s", current_path);
    }
    fb_draw_string(0, 0, path_buf);
    fb_draw_hline(0, 9, 128);
    
    if (g_browser_item_count == 0) {
        fb_draw_string(10, 24, "Empty Directory");
        fb_draw_string(10, 36, "LONG BACK: Up");
    } else {
        // Compute visible range (5 items visible lines 12..51)
        int top = selected_index - 2;
        if (top < 0) top = 0;
        if (top + 5 > g_browser_item_count && g_browser_item_count >= 5) {
            top = g_browser_item_count - 5;
        }
        if (top < 0) top = 0;
        
        static int last_browser_selected_idx = -1;
        static int browser_scroll_offset = 0;
        static uint32_t last_browser_scroll_time = 0;
        
        if (selected_index != last_browser_selected_idx) {
            last_browser_selected_idx = selected_index;
            browser_scroll_offset = 0;
            last_browser_scroll_time = (uint32_t)(esp_timer_get_time() / 1000);
        }

        for (int i = 0; i < 5; i++) {
            int idx = top + i;
            if (idx >= g_browser_item_count) break;
            
            int y = 12 + (i * 9);
            bool is_sel = (idx == selected_index);
            
            char line_buf[128];
            browser_item_t *item = &g_browser_items[idx];
            
            char display_text[MAX_NAME_LEN + 4];
            if (item->is_dir) {
                snprintf(display_text, sizeof(display_text), "[%s]", item->name);
            } else {
                snprintf(display_text, sizeof(display_text), "%s", item->name);
            }
            
            int full_len = strlen(display_text);
            
            if (is_sel) {
                if (full_len > 19) {
                    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
                    if (now - last_browser_scroll_time > OLED_SCROLL_INTERVAL_MS) {
                        browser_scroll_offset = (browser_scroll_offset + 1) % (full_len + 4);
                        last_browser_scroll_time = now;
                    }
                    
                    char scroll_win[20];
                    for (int k = 0; k < 19; k++) {
                        int pos = (browser_scroll_offset + k) % (full_len + 4);
                        if (pos < full_len) {
                            scroll_win[k] = display_text[pos];
                        } else {
                            scroll_win[k] = ' ';
                        }
                    }
                    scroll_win[19] = '\0';
                    snprintf(line_buf, sizeof(line_buf), "> %s", scroll_win);
                } else {
                    snprintf(line_buf, sizeof(line_buf), "> %s", display_text);
                }
            } else {
                snprintf(line_buf, sizeof(line_buf), "  %s", display_text);
                line_buf[21] = '\0';
            }
            
            fb_draw_string(0, y, line_buf);
        }
    }
    
    // Footer line
    fb_draw_hline(0, 56, 128);
    char footer_buf[64];
    snprintf(footer_buf, sizeof(footer_buf), "%d/%d | BACK: Up", 
             g_browser_item_count > 0 ? selected_index + 1 : 0, g_browser_item_count);
    footer_buf[21] = '\0';
    fb_draw_string(0, 57, footer_buf);
    
    fb_flush();
}

void oled_show_message(const char *line1, const char *line2) {
    fb_clear();
    if (line1) fb_draw_string(0, 24, line1);
    if (line2) fb_draw_string(0, 32, line2);
    fb_flush();
}

void oled_clear(void) {
    fb_clear();
    fb_flush();
}
