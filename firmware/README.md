# Retro Audio Guestbook: firmware

```
firmware/
├── components/rg/       shared modules, reused by the final application
│   ├── rg_board         pin map (Kconfig), safe idle levels
│   ├── rg_power         switched 5 V peripheral rail, reference counted (energy)
│   ├── rg_system        NVS, boot counter, crash-loop guard, crash-dump detection, version
│   ├── rg_hook          hook switch: debounce, deep-sleep wake
│   ├── rg_oled          SSD1306 128x64, Rubik Bold bitmap fonts (Turkish), 5x7 diagnostics font, warm init, I2C recovery, panel sleep
│   ├── rg_led           WS2812B effects task, brightness cap, holds the rail while lit
│   ├── rg_storage       SD over SPI + FAT mount; powers the rail, releases pins on unmount
│   ├── rg_wav           WAV parse / never-overwrite writer / checkpoint / power-cut repair
│   ├── rg_recorder      ring buffer + SD writer task (watchdog, CRC-32, stop deadline, recovery)
│   ├── rg_catalog       message count, never-reused numbering, manifest.csv, space left
│   ├── rg_settings      /audio/config.txt parser with range clamping
│   ├── rg_eventlog      /audio/log.txt diagnostics (rotated)
│   ├── rg_audio         I2S0 TX/RX/DUPLEX, WAV playback
│   ├── rg_mic_conv      mic word -> int16: gain, DC blocker, saturation
│   ├── rg_crc32         zlib-compatible CRC-32
│   └── rg_err           project error codes (RG_ERR_*)
├── hwtest/              Phase 2 hardware test firmware (menu over USB)
├── guestbook/           the application: gb_fsm (state machine), gb_audio (session engine),
│                        gb_ui (OLED service), gb_soak (automated Phase 5 soak test)
├── test/host/           host unit tests for the hardware-independent modules
├── sdkconfig.common     shared build settings (80 MHz, watchdog panic, crash dumps, LFN)
├── partitions.csv       nvs + app + coredump
└── .clang-format        code style (CI checks it)
```

Application design (state machine diagram, tasks, timing, failure handling): [`docs/architecture.md`](../docs/architecture.md).

- Framework: **ESP-IDF v6.1.1**, target ESP32-C3.
- Console: built-in USB-Serial-JTAG (GPIO20/21 are taken by the SD card).
- Toolchain install, build, flashing, SD card and audio file preparation: see the [top-level README](../README.md).

## Design rules

- **Power:** every driver that touches a switched-rail peripheral calls `rg_power_acquire()` before driving its pins and `rg_power_release()` after floating them. The rail (SD, amp, LED, mic) is off whenever nobody holds it, and always in deep sleep.
- **Saving:** a message counts only after `rg_rec_stop()` returns `ESP_OK`. That means the header was rewritten, the file fsync'ed and closed, then re-opened and verified. `RG_ERR_TIMEOUT` means reboot; the next mount repairs the file.
- **Writes:** NVS is written once per cold boot and three times per saved message. Deep-sleep wakes use RTC-memory caches.
- **Warnings:** project code builds with `-Wall -Wextra -Werror`.

## Host unit tests

```sh
cmake -S firmware/test/host -B build/host && cmake --build build/host && ctest --test-dir build/host --output-on-failure
```

Covered:
- WAV writer, parser and power-cut repair
- CRC-32
- mic conversion
- `config.txt` parsing
- catalog scan / naming / manifest

CI (`.github/workflows/firmware.yml`) runs these tests, the clang-format check and the ESP-IDF build.

## Crash dumps

After a crash or watchdog reset, the boot banner says `crash dump stored`. Read it with:

```sh
idf.py -p PORT coredump-info
```

Only the first dump is kept (`CONFIG_ESP_COREDUMP_FLASH_NO_OVERWRITE`). After reading it, clear the partition so the next crash is recorded:

```sh
python -m esptool --chip esp32c3 -p PORT erase-region 0x1F0000 0x10000
```

## Phase 2 hardware tests

Flash `hwtest`, open the monitor and press a number. Each test ends with one line such as:

```
RESULT T5 PASS mount_khz=20000 io=ok verify=ok write_kBps=410 ... ring_kb=32 free_mb=14850
```

Paste every `RESULT` line back. Phase 3 is tuned from these values.

| # | Test | Before running | What you do |
|---|---|---|---|
| 1 | Board & wiring | all pull resistors fitted, **handset on the cradle** | nothing. Each pull resistor (incl. the PWR_EN pull-down) is checked by driving and releasing the pin; the board then reboots. |
| 2 | OLED | — | confirm every UI screen is readable |
| 3 | WS2812B | — | confirm the 6 state colours/effects, LED dark at the end |
| 4 | Hook switch | — | lift/replace the handset ≥3 times; measures contact bounce |
| 5 | MicroSD | card inserted, ≥12 MB free | nothing. Writes and verifies one full 180 s file, measures SD latency, prints recovery/config/catalog status. |
| 6 | Microphone | handset connected through the real cord | speak for 10 s; records `TEST_MIC.wav` and plays it back |
| 7 | Amplifier | greeting files on the card | pick the comfortable tone step; listen for pops at rail power-up/down |
| 8 | TX→RX switching | — | listen to 42 beeps; speak after the first beep of each mode; listen to the WAVs for noise from the idle amp |
| 9 | Deep sleep | USB power meter on the 5 V input | pick a standby screen (full / minimal / off); hang up / lift 20+ times; read the standby current; repeat per screen variant |

After every test the menu prints `Peripheral rail: off`. If it says `ON (leak!)`, a module forgot to release the rail; report it.

The test WAVs (`TEST_MIC.wav`, `TEST_DUPLEX.wav`, `TEST_SEQ.wav`) are left in `/audio/recordings/` so you can listen on a PC. Delete them before the event; the final firmware only counts `MSG_*.wav`.
