#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t sd_card_init(void);
void sd_card_deinit(void);
int sd_card_scan_audio(const char *target_dir);
int sd_card_list_dir(const char *dir_path);
int sd_card_list_txt_dir(const char *dir_path);
int sd_card_count_ebooks(void);
void sd_card_get_storage_info(uint64_t *used_mb, uint64_t *total_mb);
bool sd_card_is_mounted(void);
