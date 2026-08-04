#include "ereader.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static const char *TAG = "EREADER";
#define EREADER_SAVE_FILE SD_MOUNT_POINT "/.ereader_save.dat"
#define MAX_PAGES 2048

typedef struct {
    char filepath[MAX_PATH_LEN];
    uint32_t file_offset;
    int current_page;
    int auto_scroll_sec;
    uint32_t bookmarks[MAX_EREADER_BOOKMARKS];
    int bookmark_count;
} ereader_save_data_t;

static FILE *s_file = NULL;
static ereader_info_t s_info = {0};
static uint32_t s_page_offsets[MAX_PAGES];
static uint32_t s_last_autoscroll_time = 0;
static bool s_is_open = false;

static void save_progress(void) {
    if (!s_is_open) return;
    FILE *f = fopen(EREADER_SAVE_FILE, "wb");
    if (f) {
        ereader_save_data_t save = {0};
        strncpy(save.filepath, s_info.filepath, MAX_PATH_LEN - 1);
        save.file_offset = (s_info.current_page < s_info.total_pages) ? s_page_offsets[s_info.current_page] : 0;
        save.current_page = s_info.current_page;
        save.auto_scroll_sec = s_info.auto_scroll_sec;
        save.bookmark_count = s_info.bookmark_count;
        memcpy(save.bookmarks, s_info.bookmarks, sizeof(s_info.bookmarks));
        
        fwrite(&save, sizeof(save), 1, f);
        fclose(f);
        ESP_LOGI(TAG, "Saved E-Reader progress for %s at page %d", s_info.filepath, s_info.current_page);
    }
}

static void load_progress(const char *filepath) {
    FILE *f = fopen(EREADER_SAVE_FILE, "rb");
    if (f) {
        ereader_save_data_t save = {0};
        if (fread(&save, sizeof(save), 1, f) == 1) {
            if (strcmp(save.filepath, filepath) == 0) {
                s_info.current_page = save.current_page;
                s_info.auto_scroll_sec = save.auto_scroll_sec;
                s_info.bookmark_count = save.bookmark_count;
                memcpy(s_info.bookmarks, save.bookmarks, sizeof(save.bookmarks));
                ESP_LOGI(TAG, "Restored E-Reader progress at page %d", s_info.current_page);
            }
        }
        fclose(f);
    }
}

// Build page offsets array by reading through lines with word wrap
static void build_page_index(void) {
    if (!s_file) return;
    
    fseek(s_file, 0, SEEK_SET);
    s_info.total_pages = 0;
    s_page_offsets[0] = 0;
    
    uint32_t current_offset = 0;
    char buffer[512];
    
    while (s_info.total_pages < MAX_PAGES - 1) {
        s_page_offsets[s_info.total_pages] = current_offset;
        s_info.total_pages++;
        
        // Scan 6 lines for current page
        fseek(s_file, current_offset, SEEK_SET);
        int lines = 0;
        
        while (lines < 6) {
            long line_start = ftell(s_file);
            if (!fgets(buffer, sizeof(buffer), s_file)) {
                // EOF reached
                return;
            }
            
            size_t len = strlen(buffer);
            if (len == 0) break;
            
            // Remove trailing \r or \n
            while (len > 0 && (buffer[len-1] == '\n' || buffer[len-1] == '\r')) {
                len--;
            }
            
            if (len == 0) {
                // Empty line counts as 1 line
                lines++;
                current_offset = ftell(s_file);
                continue;
            }
            
            // Wrap text into 21-char chunks
            size_t pos = 0;
            while (pos < len && lines < 6) {
                size_t chunk = len - pos;
                if (chunk > 21) {
                    chunk = 21;
                    // Try breaking at space
                    while (chunk > 5 && !isspace((unsigned char)buffer[pos + chunk])) {
                        chunk--;
                    }
                    if (chunk == 5 && !isspace((unsigned char)buffer[pos + chunk])) {
                        chunk = 21; // Hard break if no space found
                    }
                }
                
                pos += chunk;
                while (pos < len && isspace((unsigned char)buffer[pos])) {
                    pos++; // Skip trailing space
                }
                lines++;
            }
            
            current_offset = line_start + pos;
            if (pos >= len) {
                // Read next file line if newline was reached
                current_offset = ftell(s_file);
            }
        }
    }
}

esp_err_t ereader_open(const char *filepath) {
    ereader_close();
    
    s_file = fopen(filepath, "r");
    if (!s_file) {
        ESP_LOGE(TAG, "Failed to open txt file: %s", filepath);
        return ESP_FAIL;
    }
    
    memset(&s_info, 0, sizeof(s_info));
    strncpy(s_info.filepath, filepath, MAX_PATH_LEN - 1);
    
    build_page_index();
    load_progress(filepath);
    
    if (s_info.current_page >= s_info.total_pages) {
        s_info.current_page = 0;
    }
    
    s_is_open = true;
    s_last_autoscroll_time = (uint32_t)(esp_timer_get_time() / 1000);
    ESP_LOGI(TAG, "Opened %s with %d pages", filepath, s_info.total_pages);
    return ESP_OK;
}

void ereader_close(void) {
    if (!s_is_open) return;
    save_progress();
    if (s_file) {
        fclose(s_file);
        s_file = NULL;
    }
    s_is_open = false;
}

void ereader_next_page(void) {
    if (!s_is_open) return;
    if (s_info.current_page < s_info.total_pages - 1) {
        s_info.current_page++;
        s_last_autoscroll_time = (uint32_t)(esp_timer_get_time() / 1000);
    }
}

void ereader_prev_page(void) {
    if (!s_is_open) return;
    if (s_info.current_page > 0) {
        s_info.current_page--;
        s_last_autoscroll_time = (uint32_t)(esp_timer_get_time() / 1000);
    }
}

void ereader_get_page_lines(char lines[6][22]) {
    for (int i = 0; i < 6; i++) {
        lines[i][0] = '\0';
    }
    
    if (!s_is_open || !s_file || s_info.total_pages == 0) return;
    
    uint32_t offset = s_page_offsets[s_info.current_page];
    fseek(s_file, offset, SEEK_SET);
    
    char buffer[512];
    int line_cnt = 0;
    
    while (line_cnt < 6 && fgets(buffer, sizeof(buffer), s_file)) {
        size_t len = strlen(buffer);
        while (len > 0 && (buffer[len-1] == '\n' || buffer[len-1] == '\r')) {
            len--;
        }
        buffer[len] = '\0';
        
        if (len == 0) {
            lines[line_cnt][0] = '\0';
            line_cnt++;
            continue;
        }
        
        size_t pos = 0;
        while (pos < len && line_cnt < 6) {
            size_t chunk = len - pos;
            if (chunk > 21) {
                chunk = 21;
                while (chunk > 5 && !isspace((unsigned char)buffer[pos + chunk])) {
                    chunk--;
                }
                if (chunk == 5 && !isspace((unsigned char)buffer[pos + chunk])) {
                    chunk = 21;
                }
            }
            
            strncpy(lines[line_cnt], buffer + pos, chunk);
            lines[line_cnt][chunk] = '\0';
            line_cnt++;
            
            pos += chunk;
            while (pos < len && isspace((unsigned char)buffer[pos])) {
                pos++;
            }
        }
    }
}

void ereader_add_bookmark(void) {
    if (!s_is_open) return;
    uint32_t page = s_info.current_page;
    for (int i = 0; i < s_info.bookmark_count; i++) {
        if (s_info.bookmarks[i] == page) return; // Already bookmarked
    }
    if (s_info.bookmark_count < MAX_EREADER_BOOKMARKS) {
        s_info.bookmarks[s_info.bookmark_count++] = page;
    } else {
        // Shift bookmarks left
        memmove(&s_info.bookmarks[0], &s_info.bookmarks[1], sizeof(uint32_t) * (MAX_EREADER_BOOKMARKS - 1));
        s_info.bookmarks[MAX_EREADER_BOOKMARKS - 1] = page;
    }
    save_progress();
}

int ereader_get_bookmark_count(void) {
    return s_info.bookmark_count;
}

bool ereader_jump_to_bookmark(int index) {
    if (!s_is_open || index < 0 || index >= s_info.bookmark_count) return false;
    s_info.current_page = s_info.bookmarks[index];
    s_last_autoscroll_time = (uint32_t)(esp_timer_get_time() / 1000);
    return true;
}

void ereader_cycle_autoscroll(void) {
    if (s_info.auto_scroll_sec == 0) s_info.auto_scroll_sec = 5;
    else if (s_info.auto_scroll_sec == 5) s_info.auto_scroll_sec = 10;
    else if (s_info.auto_scroll_sec == 10) s_info.auto_scroll_sec = 15;
    else s_info.auto_scroll_sec = 0;
    
    s_last_autoscroll_time = (uint32_t)(esp_timer_get_time() / 1000);
}

bool ereader_check_autoscroll(void) {
    if (!s_is_open || s_info.auto_scroll_sec == 0) return false;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if ((now - s_last_autoscroll_time) >= (s_info.auto_scroll_sec * 1000)) {
        if (s_info.current_page < s_info.total_pages - 1) {
            s_info.current_page++;
            s_last_autoscroll_time = now;
            return true;
        }
    }
    return false;
}

const ereader_info_t* ereader_get_info(void) {
    return &s_info;
}

bool ereader_is_open(void) {
    return s_is_open;
}
