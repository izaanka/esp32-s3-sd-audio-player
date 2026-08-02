# ESP32-S3 USB-C DAC SD Sound Player

A high-performance, standalone audio player built for the ESP32-S3 microcontroller using ESP-IDF v5.1. Audio is output directly over USB Host to standard USB-C DAC dongles and USB-C headphones using native USB Audio Class 1.0 (UAC), with track navigation, volume control, and metadata rendered on an SSD1306 OLED display.

---

## Direct Binary Download

Pre-compiled binary files are committed directly to this repository in the [`bin/`](./bin) directory and updated automatically on every commit via GitHub Actions:

- **Single Merged Binary (Recommended)**: [`bin/esp32s3-sd-audio-player-merged.bin`](./bin/esp32s3-sd-audio-player-merged.bin)
- **Application Binary**: [`bin/sound_test.bin`](./bin/sound_test.bin)
- **Bootloader Binary**: [`bin/bootloader.bin`](./bin/bootloader.bin)
- **Partition Table Binary**: [`bin/partition-table.bin`](./bin/partition-table.bin)

---

## Hardware Requirements and Wiring

| Component | Pin / Interface | ESP32-S3 Pin |
| :--- | :--- | :--- |
| **USB-C DAC Output** | USB D- | **GPIO 19** |
| | USB D+ | **GPIO 20** |
| | VBUS Power Enable | **GPIO 18** & **GPIO 38** (HIGH) / 5V VBUS |
| **SSD1306 OLED (128x64)** | I2C SDA | **GPIO 8** |
| | I2C SCL | **GPIO 9** |
| | Address | `0x3C` |
| **SD Card Module (SPI)** | MOSI | **GPIO 11** |
| | MISO | **GPIO 13** |
| | SCK (SCLK) | **GPIO 12** |
| | CS (Chip Select) | **GPIO 10** |
| **Navigation Buttons** | UP (Vol+ / Next) | **GPIO 4** (Internal Pull-Up) |
| | DOWN (Vol- / Prev) | **GPIO 5** (Internal Pull-Up) |
| | SELECT (Play / Pause) | **GPIO 6** (Internal Pull-Up) |
| | BACK (Loop Selector) | **GPIO 7** (Internal Pull-Up) |

---

## Features

- **Native USB Host Audio Output**: Streams 16-bit Stereo PCM audio to standard USB Audio Class 1.0 (UAC) DAC devices.
- **Realtime Sample Rate Converter (SRC)**: Resamples 44.1kHz (or other input sample rates) to 48,000 Hz stereo in real-time to support DACs with fixed 48kHz hardware endpoints.
- **High Throughput and Streaming**: 
  - **20 MHz SPI** bus clock for the SD Card.
  - **16 KB stream buffering** (`setvbuf`) for high-speed sector reads.
  - **32 KB UAC hardware ring-buffer** for buffer-underrun protection.
- **Audio Decoders**:
  - **WAV**: Uncompressed PCM (8/16-bit, Mono/Stereo, arbitrary sample rates).
  - **MP3**: High-efficiency LibHelix fixed-point MP3 decoder with automatic ID3v2 tag skipping.
- **4 Playback Loop Modes**:
  - `Seq` (Sequential): Plays all tracks and stops at the end of the SD card.
  - `All` (Loop All): Repeats the playlist continuously.
  - `One` (Loop One): Repeats the current track continuously.
  - `Shuf` (Shuffle): Plays tracks in random order.
- **Dual Volume Control**:
  - **Hardware Volume**: Adjusts physical DAC hardware output directly via UAC control transfers.
  - **Software Volume**: Scales PCM sample amplitudes in software.
  - **Default Volume**: 50% on startup.
- **Rich SSD1306 OLED UI**:
  - Live Header: Volume level (`V:50%`).
  - Track Index: `Track X / Y`.
  - Auto-scrolling long filenames.
  - Audio Format: `Sample Rate`, `Bit Depth`, `Stereo/Mono`.
  - Progress Bar and Elapsed/Total Duration (`MM:SS`).
  - DAC Connection Status (`DAC:OK` vs `NO DAC`).

---

## User Controls

| Button | Short Press | Long Press (>0.8s) |
| :--- | :--- | :--- |
| **`UP` (GPIO 4)** | Volume Up (+5%) | Next Track / Next Random Track |
| **`DOWN` (GPIO 5)** | Volume Down (-5%) | Previous Track |
| **`SELECT` (GPIO 6)** | Play / Pause Toggle | — |
| **`BACK` (GPIO 7)** | Cycle Loop Mode (`Seq` → `All` → `One` → `Shuf`) | — |

---

## Flashing Methods

### Method 1: Web App Browser Flashing (Primary / Recommended)

Flash the ESP32-S3 directly from your web browser using Web Serial (Google Chrome, Microsoft Edge, or Opera):

1. Connect your ESP32-S3 board to your computer via USB cable.
2. Download the pre-compiled single binary: [`esp32s3-sd-audio-player-merged.bin`](./bin/esp32s3-sd-audio-player-merged.bin).
3. Open a Web Serial ESP Flasher tool:
   - [adafruit.github.io/web-serial-esptool](https://adafruit.github.io/web-serial-esptool/)
   - [esp.toit.io](https://esp.toit.io/)
4. Click **Connect** and select your ESP32-S3 serial port.
5. Select the downloaded **`esp32s3-sd-audio-player-merged.bin`** file and set the offset to **`0x00000`**.
6. Click **Program / Flash**.

---

### Method 2: Single Merged Binary Command Line (`esptool.py`)

Download [`bin/esp32s3-sd-audio-player-merged.bin`](./bin/esp32s3-sd-audio-player-merged.bin) and flash at offset `0x00000`:

```bash
esptool.py --chip esp32s3 -p /dev/ttyACM0 -b 460800 write_flash 0x00000 bin/esp32s3-sd-audio-player-merged.bin
```

---

### Method 3: Flash Individual Component Binaries

Using component binaries from the [`bin/`](./bin) directory:

```bash
esptool.py --chip esp32s3 -p /dev/ttyACM0 -b 460800 \
  --before=default_reset --after=hard_reset write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x00000 bin/bootloader.bin \
  0x08000 bin/partition-table.bin \
  0x10000 bin/sound_test.bin
```

---

### Method 4: Building from Source using ESP-IDF CLI

1. Set up the ESP-IDF environment:
```bash
source ~/esp/esp-idf/export.sh
```

2. Build the project binaries:
```bash
idf.py build
```

3. Flash to the ESP32-S3 board and launch the serial monitor:
```bash
idf.py -p /dev/ttyACM0 flash monitor
```

---

## Project Architecture

```text
sound_test/
├── bin/                       # Pre-compiled .bin firmware files for direct flashing
│   ├── esp32s3-sd-audio-player-merged.bin
│   ├── sound_test.bin
│   ├── bootloader.bin
│   └── partition-table.bin
├── main/
│   ├── main.c                 # app_main entry point, startup staging & UI task
│   ├── config.h               # Hardware pin definitions & global configuration
│   ├── audio_player.c/h       # Playback state machine & command queue handler
│   ├── usb_audio_output.c/h   # USB Host UAC driver, volume API & 48kHz resampler
│   ├── sd_card.c/h            # FatFS SPI SD card driver & track scanner
│   ├── wav_decoder.c/h        # RIFF WAV parser with 16KB stream buffering
│   ├── mp3_decoder.c/h        # LibHelix MP3 decoder with ID3v2 parser
│   ├── button_handler.c/h     # 4-button debouncer & short/long-press detector
│   ├── oled_display.c/h       # SSD1306 I2C OLED display renderer
│   └── font5x7.h              # 5x7 bitmap font for OLED text
├── .github/workflows/build.yml # Automated CI/CD build & auto-commit workflow
├── sdkconfig.defaults         # Default Kconfig settings (USB Host Periodic OUT FIFO bias)
├── CMakeLists.txt             # Main CMake project configuration
└── README.md                  # Documentation
```
