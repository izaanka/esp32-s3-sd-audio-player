#pragma once
#include "config.h"
#include "esp_err.h"

esp_err_t oled_init(void);
void oled_update(const player_state_t *state);
void oled_draw_boot_screen(int selected_index);
void oled_draw_browser(const char *current_path, int selected_index);
void oled_draw_ereader_page(void);
void oled_draw_ereader_menu(int menu_index);
void oled_draw_autoscroll_menu(int selected_index);
void oled_draw_bookmark_menu(int selected_index);
void oled_draw_goto_page_menu(const int digits[4], int active_digit, int total_pages);
void oled_draw_settings_screen(int ebooks_count, int music_count, uint64_t sd_used_mb, uint64_t sd_total_mb);
void oled_show_message(const char *line1, const char *line2);
void oled_clear(void);
void oled_set_sleep(bool sleep);
bool oled_is_sleeping(void);

// Raw Framebuffer primitives for Arcade
void oled_fb_clear(void);
void oled_fb_flush(void);
void oled_fb_draw_string(int x, int y, const char *str);
void oled_fb_draw_rect(int x, int y, int w, int h);
void oled_fb_fill_rect(int x, int y, int w, int h);
void oled_fb_draw_line(int x0, int y0, int x1, int y1);
