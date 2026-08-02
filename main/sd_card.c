#include "sd_card.h"
#include "config.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>

static const char *TAG = "SD_CARD";
static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;

static void str_to_lower(char *str) {
    for (int i = 0; str[i]; i++) {
        str[i] = tolower((unsigned char)str[i]);
    }
}

static bool has_extension(const char *filename, const char *ext) {
    size_t len = strlen(filename);
    size_t ext_len = strlen(ext);
    if (len < ext_len) return false;
    
    char file_ext[8];
    strncpy(file_ext, filename + len - ext_len, sizeof(file_ext));
    file_ext[sizeof(file_ext) - 1] = '\0';
    str_to_lower(file_ext);
    
    char check_ext[8];
    strncpy(check_ext, ext, sizeof(check_ext));
    check_ext[sizeof(check_ext) - 1] = '\0';
    str_to_lower(check_ext);
    
    return strcmp(file_ext, check_ext) == 0;
}

static void extract_name(const char *filename, char *out_name, size_t max_len) {
    const char *ext = strrchr(filename, '.');
    size_t name_len = ext ? (size_t)(ext - filename) : strlen(filename);
    if (name_len >= max_len) name_len = max_len - 1;
    strncpy(out_name, filename, name_len);
    out_name[name_len] = '\0';
}

static int compare_tracks(const void *a, const void *b) {
    const track_info_t *ta = (const track_info_t *)a;
    const track_info_t *tb = (const track_info_t *)b;
    return strcasecmp(ta->name, tb->name);
}

esp_err_t sd_card_init(void) {
    if (s_mounted) return ESP_OK;
    
    ESP_LOGI(TAG, "Initializing SD card via SPI (MOSI: %d, MISO: %d, SCK: %d, CS: %d)",
             PIN_SD_MOSI, PIN_SD_MISO, PIN_SD_SCK, PIN_SD_CS);
    
    // Explicitly configure internal pull-ups on SPI pins
    gpio_set_pull_mode(PIN_SD_MISO, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_SD_MOSI, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_SD_CS,   GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_SD_SCK,  GPIO_PULLUP_ONLY);
    
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_SD_MOSI,
        .miso_io_num = PIN_SD_MISO,
        .sclk_io_num = PIN_SD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus (err 0x%x)", ret);
        return ret;
    }
    
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 20000; // 20 MHz high speed for lag-free audio streaming
    
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_SD_CS;
    slot_config.host_id = SPI2_HOST;
    slot_config.gpio_cd = SDSPI_SLOT_NO_CD;
    slot_config.gpio_wp = SDSPI_SLOT_NO_WP;
    
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(200));
        ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card (err 0x%x). Ensure SD card is inserted & formatted as FAT32.", ret);
        spi_bus_free(SPI2_HOST);
        return ret;
    }
    
    s_mounted = true;
    ESP_LOGI(TAG, "SD Card mounted successfully!");
    sdmmc_card_print_info(stdout, s_card);
    
    return ESP_OK;
}

void sd_card_deinit(void) {
    if (!s_mounted) return;
    
    esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    ESP_LOGI(TAG, "Card unmounted");
    
    // We optionally deinitialize the SPI bus here, but it could be shared.
    // For simplicity, we just leave the bus initialized.
    s_mounted = false;
    s_card = NULL;
}

static void scan_dir(const char *dir_path, int depth) {
    if (depth > 1) return; // Only root and one level deep
    
    DIR *dir = opendir(dir_path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s", dir_path);
        return;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && g_track_count < MAX_TRACKS) {
        // Ignore hidden files and dotfiles (e.g. ._song.mp3, .DS_Store)
        if (entry->d_name[0] == '.') {
            continue;
        }
        
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                scan_dir(full_path, depth + 1);
            } else if (S_ISREG(st.st_mode)) {
                audio_format_t format = FORMAT_UNKNOWN;
                if (has_extension(entry->d_name, ".wav")) {
                    format = FORMAT_WAV;
                } else if (has_extension(entry->d_name, ".mp3")) {
                    format = FORMAT_MP3;
                }
                
                if (format != FORMAT_UNKNOWN) {
                    track_info_t *track = &g_tracks[g_track_count];
                    strncpy(track->path, full_path, MAX_PATH_LEN - 1);
                    track->path[MAX_PATH_LEN - 1] = '\0';
                    extract_name(entry->d_name, track->name, MAX_NAME_LEN);
                    track->format = format;
                    g_track_count++;
                }
            }
        }
    }
    
    closedir(dir);
}

int sd_card_scan_audio(void) {
    g_track_count = 0;
    if (!s_mounted) return 0;
    
    scan_dir(SD_MOUNT_POINT, 0);
    
    if (g_track_count > 0) {
        qsort(g_tracks, g_track_count, sizeof(track_info_t), compare_tracks);
    }
    
    ESP_LOGI(TAG, "Found %d audio files", g_track_count);
    return g_track_count;
}

bool sd_card_is_mounted(void) {
    return s_mounted;
}
