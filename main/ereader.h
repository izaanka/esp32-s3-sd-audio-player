#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "config.h"

#define MAX_EREADER_BOOKMARKS 5

typedef struct {
    char filepath[MAX_PATH_LEN];
    uint32_t file_offset;
    int current_page;
    int total_pages;
    int auto_scroll_sec; // 0=Off, 5, 10, 15
    uint32_t bookmarks[MAX_EREADER_BOOKMARKS];
    int bookmark_count;
} ereader_info_t;

esp_err_t ereader_open(const char *filepath);
void ereader_close(void);

void ereader_next_page(void);
void ereader_prev_page(void);
void ereader_get_page_lines(char lines[6][22]);

void ereader_add_bookmark(void);
int ereader_get_bookmark_count(void);
bool ereader_jump_to_bookmark(int index);

void ereader_cycle_autoscroll(void);
bool ereader_check_autoscroll(void);

const ereader_info_t* ereader_get_info(void);
bool ereader_is_open(void);
