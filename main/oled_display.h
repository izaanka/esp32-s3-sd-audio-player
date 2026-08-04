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
void oled_show_message(const char *line1, const char *line2);
void oled_clear(void);
void oled_set_sleep(bool sleep);
bool oled_is_sleeping(void);
