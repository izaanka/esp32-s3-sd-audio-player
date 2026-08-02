#pragma once
// =====================================================================
// ESP-IDF Sound Player — Configuration & Shared Definitions
// Target: ESP32-S3 N16R8 (16MB Flash, 8MB PSRAM)
// Framework: ESP-IDF v5.x (pure C)
// =====================================================================

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// =====================================================================
// GPIO PIN DEFINITIONS
// =====================================================================

// --- SD Card (SPI Bus, shared SPI2) ---
#define PIN_SD_MOSI          11
#define PIN_SD_MISO          13
#define PIN_SD_SCK           12
#define PIN_SD_CS            10

// --- SSD1306 OLED (I2C) ---
#define PIN_OLED_SDA         8
#define PIN_OLED_SCL         9
#define OLED_I2C_ADDR        0x3C
#define OLED_WIDTH           128
#define OLED_HEIGHT          64

// --- I2S DAC Output ---
#define PIN_I2S_BCK          14
#define PIN_I2S_WS           15
#define PIN_I2S_DOUT         16

// --- Navigation Buttons (Active LOW, internal pull-up) ---
#define PIN_BTN_UP           4      // Vol+ (short), Next (long)
#define PIN_BTN_DOWN         5      // Vol- (short), Prev (long)
#define PIN_BTN_SELECT       6      // Play / Pause
#define PIN_BTN_BACK         7      // Loop mode toggle

// =====================================================================
// AUDIO CONSTANTS
// =====================================================================
#define AUDIO_SAMPLE_RATE_DEFAULT  44100
#define AUDIO_BIT_DEPTH_DEFAULT    16
#define AUDIO_CHANNELS_DEFAULT     2

#define AUDIO_DMA_BUF_COUNT        6
#define AUDIO_DMA_BUF_LEN          1024    // Samples per DMA buffer

// PCM decode output buffer (enough for one MP3 frame: 1152 stereo samples)
#define PCM_BUF_SIZE               (1152 * 2 * 2)  // 1152 samples * 2ch * 2 bytes

// MP3 read buffer
#define MP3_READBUF_SIZE           2048

// Volume
#define VOLUME_DEFAULT             50
#define VOLUME_MIN                 0
#define VOLUME_MAX                 100
#define VOLUME_STEP                5

// =====================================================================
// FILE SCANNING
// =====================================================================
#define SD_MOUNT_POINT             "/sdcard"
#define MAX_TRACKS                 128
#define MAX_PATH_LEN               256
#define MAX_NAME_LEN               64

// =====================================================================
// BUTTON CONSTANTS
// =====================================================================
#define BTN_DEBOUNCE_MS            50
#define BTN_LONG_PRESS_MS          800
#define BTN_POLL_INTERVAL_MS       10

// =====================================================================
// DISPLAY CONSTANTS
// =====================================================================
#define OLED_REFRESH_INTERVAL_MS   100     // ~10 FPS
#define OLED_SCROLL_INTERVAL_MS    300     // Long filename scroll speed
#define OLED_SCROLL_CHARS_VISIBLE  21      // Max chars per line with 5x7 font + 1px gap

// =====================================================================
// ENUMERATIONS
// =====================================================================

/// Player commands (sent via queue from UI task to audio task)
typedef enum {
    CMD_NONE = 0,
    CMD_PLAY,
    CMD_PAUSE,
    CMD_RESUME,
    CMD_STOP,
    CMD_NEXT,
    CMD_PREV,
    CMD_VOL_UP,
    CMD_VOL_DOWN,
    CMD_LOOP_TOGGLE,
} player_cmd_t;

/// Player status
typedef enum {
    STATUS_IDLE = 0,
    STATUS_SCANNING,
    STATUS_STOPPED,
    STATUS_PLAYING,
    STATUS_PAUSED,
    STATUS_ERROR,
} player_status_t;

/// Loop mode
typedef enum {
    LOOP_SEQUENTIAL = 0,   // Play all, stop at end
    LOOP_ALL,              // Play all, repeat from start
    LOOP_ONE,              // Repeat current track
    LOOP_SHUFFLE,          // Random playback
    LOOP_MODE_COUNT,
} loop_mode_t;

/// Audio file format
typedef enum {
    FORMAT_UNKNOWN = 0,
    FORMAT_WAV,
    FORMAT_MP3,
} audio_format_t;

/// Button events (decoded from GPIO)
typedef enum {
    BTN_EVT_NONE = 0,
    BTN_EVT_SELECT_SHORT,
    BTN_EVT_UP_SHORT,
    BTN_EVT_UP_LONG,
    BTN_EVT_DOWN_SHORT,
    BTN_EVT_DOWN_LONG,
    BTN_EVT_BACK_SHORT,
} button_event_t;

// =====================================================================
// DATA STRUCTURES
// =====================================================================

/// Information about a single audio track
typedef struct {
    char path[MAX_PATH_LEN];       // Full path: "/sdcard/song.mp3"
    char name[MAX_NAME_LEN];       // Display name: "song"
    audio_format_t format;
} track_info_t;

/// Shared player state (protected by mutex, read by UI, written by audio task)
typedef struct {
    player_status_t status;
    int             track_index;
    int             total_tracks;
    uint8_t         volume;        // 0–100
    loop_mode_t     loop_mode;
    uint32_t        elapsed_ms;
    uint32_t        total_ms;
    char            track_name[MAX_NAME_LEN];
    uint32_t        sample_rate;
    uint16_t        bits_per_sample;
    uint16_t        num_channels;
    char            error_msg[64];
} player_state_t;

// =====================================================================
// GLOBAL EXTERNS (defined in main.c)
// =====================================================================

/// Command queue: UI task → Audio task
extern QueueHandle_t g_cmd_queue;

/// Player state mutex
extern SemaphoreHandle_t g_state_mutex;

/// Shared player state
extern player_state_t g_player_state;

/// Track list (populated by sd_card scanner)
extern track_info_t g_tracks[MAX_TRACKS];
extern int g_track_count;

// =====================================================================
// UTILITY MACROS
// =====================================================================

/// Get loop mode name string
static inline const char* loop_mode_str(loop_mode_t m) {
    switch (m) {
        case LOOP_SEQUENTIAL: return "Sequential";
        case LOOP_ALL:        return "Loop All";
        case LOOP_ONE:        return "Loop One";
        case LOOP_SHUFFLE:    return "Shuf";
        default:              return "???";
    }
}

/// Get player status name string
static inline const char* status_str(player_status_t s) {
    switch (s) {
        case STATUS_IDLE:     return "Idle";
        case STATUS_SCANNING: return "Scanning...";
        case STATUS_STOPPED:  return "Stopped";
        case STATUS_PLAYING:  return "Playing";
        case STATUS_PAUSED:   return "Paused";
        case STATUS_ERROR:    return "Error";
        default:              return "???";
    }
}

/// Clamp integer to range
static inline int clamp_int(int val, int lo, int hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}
