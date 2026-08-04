# ESP32-S3 Multi-App Explorer & Audio Player

A feature-packed, multi-application system built for the ESP32-S3 microcontroller using ESP-IDF v5.1. Features an Audio Player with native USB Host UAC support, an E-Reader for text files, a Morse Code Translator, an Arcade Game (Lights Out), and a System Settings / Info viewer on an SSD1306 OLED display.

---

## Direct Binary Download

Pre-compiled binary files are available in the [`bin/`](./bin) directory:

- **Single Merged Binary (Recommended)**: [`bin/esp32s3-sd-audio-player-merged.bin`](./bin/esp32s3-sd-audio-player-merged.bin)
- **Application Binary**: [`bin/sound_test.bin`](./bin/sound_test.bin)
- **Bootloader Binary**: [`bin/bootloader.bin`](./bin/bootloader.bin)
- **Partition Table Binary**: [`bin/partition-table.bin`](./bin/partition-table.bin)

---

## How to Build and Upload Code

### Quick Upload Command (ESP-IDF CLI)

To build and flash the code directly to your connected ESP32-S3:

```bash
. $HOME/esp/esp-idf/export.sh && idf.py flash
```

Or specify the serial port explicitly:

```bash
. $HOME/esp/esp-idf/export.sh && idf.py -p /dev/ttyACM0 flash
```

To build, flash, and open the serial monitor all in one step:

```bash
. $HOME/esp/esp-idf/export.sh && idf.py -p /dev/ttyACM0 flash monitor
```

---

## Hardware Requirements and Pinout

| Component | Pin / Interface | ESP32-S3 Pin |
| :--- | :--- | :--- |
| **USB-C DAC Output** | USB D- | **GPIO 19** |
| | USB D+ | **GPIO 20** |
| | VBUS Power Enable | **GPIO 18** & **GPIO 38** (HIGH) / 5V VBUS |
| **SSD1306 OLED (128x64)** | I2C SDA | **GPIO 42** |
| | I2C SCL | **GPIO 41** |
| | Address | `0x3C` |
| **SD Card Module (SPI)** | MOSI | **GPIO 11** |
| | MISO | **GPIO 13** |
| | SCK (SCLK) | **GPIO 12** |
| | CS (Chip Select) | **GPIO 10** |
| **Navigation Buttons** | UP | **GPIO 4** (Internal Pull-Up) |
| | DOWN | **GPIO 5** (Internal Pull-Up) |
| | SELECT | **GPIO 6** (Internal Pull-Up) |
| | BACK | **GPIO 7** (Internal Pull-Up) |

---

## Applications & Features

The main boot menu allows navigating between 6 standalone applications:

1. **Music Player (File Browser)**:
   - Browse folders and select `.mp3` or `.wav` files.
   - Native USB Host Audio (UAC 1.0) streaming with real-time resampler (SRC) to 48kHz.
2. **Player**:
   - Live track playback interface (volume, elapsed/total time, live progress bar, track name scrolling).
   - Long press BACK returns directly to Main Menu.
3. **E-Reader**:
   - Browse and read `.txt` files as eBooks on the OLED display.
   - Saves last read position to SD Card automatically on exit.
   - Configurable auto-scroll (off, 1-10s, 15s, 20s, 25s, 30s).
   - Multi-bookmark support (save, view, jump to bookmarks).
   - "Go To Page" digit selector.
4. **Arcade**:
   - Interactive games engine starting with **Lights Out** puzzle.
   - Games listed alphabetically with fast-response leading-edge button debouncing.
5. **Morse Translator**:
   - Convert text and button signals into Morse code signals.
6. **Settings / System Info**:
   - Displays count of eBook files (`.txt`).
   - Displays count of Music files (`.mp3`/`.wav`).
   - ESP32 Flash memory capacity (16 MB).
   - SD Card storage usage (Used / Total space in MB/GB).

---

## User Controls

| Button | Short Press | Long Press (>0.8s) |
| :--- | :--- | :--- |
| **`UP` (GPIO 4)** | Up / Volume Up (+5%) | Next Track |
| **`DOWN` (GPIO 5)** | Down / Volume Down (-5%) | Previous Track |
| **`SELECT` (GPIO 6)** | Confirm / Play / Pause | — |
| **`BACK` (GPIO 7)** | Back / Cycle Loop Mode | Exit to Main Menu |

---

## Flashing Methods

### Method 1: ESP-IDF Command Line (Recommended)

```bash
. $HOME/esp/esp-idf/export.sh && idf.py flash
```

### Method 2: Command Line via `esptool.py`

Flash pre-compiled merged binary:

```bash
esptool.py --chip esp32s3 -p /dev/ttyACM0 -b 460800 write_flash 0x00000 bin/esp32s3-sd-audio-player-merged.bin
```

---

## Project Structure

```text
sound_test/
├── bin/                       # Pre-compiled .bin firmware files
├── main/
│   ├── main.c                 # app_main entry point & UI task state machine
│   ├── config.h               # Hardware pin definitions & global configuration
│   ├── audio_player.c/h       # Audio task & command queue handler
│   ├── usb_audio_output.c/h   # USB Host UAC driver & SRC resampler
│   ├── sd_card.c/h            # FatFS SPI SD card driver & file scanner
│   ├── ereader.c/h            # E-Book reader, bookmarks & auto-scroll engine
│   ├── arcade_engine.c/h      # Arcade game launcher & menu
│   ├── arcade_lightsout.c/h   # Lights Out game logic
│   ├── arcade_morse.c/h       # Morse Translator application
│   ├── button_handler.c/h     # Zero-latency leading-edge button debouncer
│   └── oled_display.c/h       # SSD1306 OLED display driver & UI widgets
├── CMakeLists.txt             # Main CMake build file
└── README.md                  # Documentation
```
