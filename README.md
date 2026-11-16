# Retro Audio Guestbook

An offline wedding/event audio guestbook built into a 3D-printed rotary telephone. A guest lifts the handset, hears a greeting and a beep, and leaves a message. Each message is saved as its own WAV file on a microSD card.

| Item | Detail |
|---|---|
| Controller | ESP32-C3 Super Mini |
| Audio | I²S MEMS microphone, MAX98357A I²S amplifier |
| Storage | microSD (SPI, 5 V module) |
| UI | SSD1306 128×32 OLED, WS2812B LED |
| Wake | mechanical hook switch on GPIO3 (deep-sleep wake) |
| Recording format | 16 kHz, mono, 16-bit PCM WAV, maximum 180 s |
| Firmware | ESP-IDF v6.1.1 (C) |

> **Project status**
> - Phase 1 (hardware validation): complete.
> - Phase 2 (hardware tests): `firmware/hwtest`.
> - Phases 3–4 (guestbook application): `firmware/guestbook`, written and building. Defaults are untuned until your hardware test results are in.
> - Phase 5 (reliability): automated soak test plus a checklist.
> - Phase 6 (enclosure): you already have one.
>
> Nothing has run on real hardware yet. Assemble and test in the order of [`docs/assembly.md`](docs/assembly.md).

```
retrodio/
├── docs/
│   ├── phase1-hardware-validation.md    BOM, pin map, wiring, power/energy, audio design, decisions
│   ├── assembly.md                      electronics build, stage by stage, with a test after each stage
│   ├── architecture.md                  state machine, tasks, timing, failure handling
│   └── testing.md                       Phase 5: soak test, manual scenarios, fault injection
├── sdcard/                              microSD template (config.txt, folder layout)
├── firmware/
│   ├── components/rg/                   shared modules (power, storage, audio, UI, recovery)
│   ├── hwtest/                          Phase 2 hardware test firmware
│   ├── guestbook/                       the guestbook application (deploy this)
│   ├── test/host/                       host unit tests
│   ├── sdkconfig.common, partitions.csv shared build settings
│   └── README.md                        firmware layout, design rules, test procedure
├── .github/workflows/firmware.yml       CI: host tests, format check, ESP-IDF builds
└── CHANGELOG.md
```

---

## 1. Hardware assembly

Wire the board exactly as in [`docs/phase1-hardware-validation.md`](docs/phase1-hardware-validation.md):
- §3 pin table
- §4 module connections and the switched power rail
- §6 power and energy budget
- §8 parts to buy, including the event spare kit

The rules that prevent damage:

- **The mic runs on 3.3 V only**, from its own small LDO on the switched rail (3V3_SW). Never connect its VDD to 5 V (absolute maximum 3.63 V).
- **The SD module, MAX98357A, WS2812B and level shifter run on the switched 5 V rail (5V_SW).** It is off in deep sleep, which is what keeps standby at ESP32 + OLED only.
- **Never connect the PC's USB and the 5 V adapter at the same time.** The Super Mini ties USB VBUS directly to its 5V pin. For debugging with the adapter attached, use a data-only USB cable.
- **Hook switch wiring:** COM to GND, NC to GPIO3, with a 10 kΩ pull-up to 3V3.
- **Energy:** desolder the Super Mini's red power LED (1–3 mA, always on).
- **ESD:** fit the TVS array at the handset cord connector; guests handle the handset.

## 2. Install the toolchain (one time)

### macOS

```sh
brew install cmake ninja dfu-util uv
mkdir -p ~/esp && cd ~/esp
git clone --depth 1 --branch v6.1.1 --recursive --shallow-submodules \
    https://github.com/espressif/esp-idf.git esp-idf-v6.1.1
```

Create the Python environment with a known-good Python. Homebrew Python 3.13+ can fail to create a virtualenv (`ensurepip` error):

```sh
uv python install 3.12
uv venv --python 3.12 --seed ~/.espressif/python_env/idf6.1_py3.12_env
export IDF_PYTHON_ENV_PATH=$HOME/.espressif/python_env/idf6.1_py3.12_env
cd ~/esp/esp-idf-v6.1.1 && ./install.sh esp32c3
```

### Linux (Debian/Ubuntu)

```sh
sudo apt-get install git wget flex bison gperf python3 python3-pip python3-venv cmake \
    ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
sudo usermod -aG dialout $USER        # serial port access; log out and back in
mkdir -p ~/esp && cd ~/esp
git clone --depth 1 --branch v6.1.1 --recursive --shallow-submodules \
    https://github.com/espressif/esp-idf.git esp-idf-v6.1.1
cd esp-idf-v6.1.1 && ./install.sh esp32c3
```

### Windows

Install ESP-IDF **v6.1.1** for ESP32-C3 with Espressif's Windows installer, following the [official Get Started guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/get-started/index.html). Then run the commands below from the "ESP-IDF PowerShell" shortcut.

### Every new shell

```sh
export IDF_PYTHON_ENV_PATH=$HOME/.espressif/python_env/idf6.1_py3.12_env   # macOS setup above only
. ~/esp/esp-idf-v6.1.1/export.sh
```

Check the result with `idf.py --version`; it should print `v6.1.1`.

## 3. Build

There are two firmware images:

| Directory | What it is | When |
|---|---|---|
| `firmware/hwtest` | hardware test menu (tests 1–9) | while assembling: [`docs/assembly.md`](docs/assembly.md) |
| `firmware/guestbook` | **the guestbook application** | after the hardware tests pass, and at events |

```sh
cd firmware/guestbook          # or firmware/hwtest
idf.py set-target esp32c3      # first time only; creates sdkconfig from ../sdkconfig.common
idf.py build
```

The first build downloads the `espressif/led_strip` component, so it needs internet once. The device itself never does.

- **Shared settings:** both projects read `firmware/sdkconfig.common` and `firmware/partitions.csv`. After changing either file, delete the project's `sdkconfig` and rebuild.
- **Compile-time tuning:** `idf.py menuconfig`.
  - **Retro Guestbook:** pins, hook polarity and debounce, SD clock, default gain/volume/LED/contrast, rail settle time.
  - **Guestbook application:** screen times, wake confirmation, save deadline, ring buffer size, soak test.
- **Field tuning:** use `/audio/config.txt` on the card (§5); no rebuild needed.

## 4. Flash (deploy to the board)

Connect the Super Mini's USB-C to the computer. The 5 V adapter must be **disconnected**, or use a data-only cable.

| OS | Serial port |
|---|---|
| macOS | `/dev/cu.usbmodem*` |
| Linux | `/dev/ttyACM0` |
| Windows | `COMx` (Device Manager) |

```sh
idf.py -p /dev/cu.usbmodem1101 flash monitor     # replace with your port; Ctrl+] exits the monitor
```

- **If the port is missing, flashing times out, or the board is in deep sleep:** hold **BOOT**, tap **RESET**, release **BOOT**, then flash again. After flashing, press **RESET** once to start the firmware.
- **The guestbook sleeps most of the time,** so its USB console disappears between sessions. That is normal; `idf.py monitor` reconnects on the next lift.

### Flashing without the toolchain

Build once on any machine, then create a single image:

```sh
idf.py merge-bin            # -> build/merged-binary.bin
```

Flash it from any computer, using either:
- the browser tool [ESP Web Flasher (esptool-js)](https://espressif.github.io/esptool-js/) at address `0x0`, or
- esptool:

```sh
pip install esptool
python -m esptool --chip esp32c3 -p PORT -b 460800 write-flash 0x0 build/merged-binary.bin
```

## 5. Prepare the microSD card

1. Use an 8–32 GB SDHC card, class A1/U1, from a known brand. A high-endurance (dashcam) card survives repeated events best.
2. Format it **FAT32** with 32 KB clusters. Use the SD Association "SD Card Formatter", or on macOS: `diskutil eraseDisk FAT32 GUESTBOOK MBRFormat /dev/diskN` (check `N` with `diskutil list`; this erases that disk).
3. Copy the template folder [`sdcard/audio/`](sdcard/) to the card root, then add your greeting files:

```
/audio/config.txt                  settings (volume, mic gain, max length, LED, contrast, standby screen)
/audio/greetings/greeting.wav
/audio/greetings/instruction.wav   optional
/audio/greetings/beep.wav
/audio/recordings/                 messages are written here (MSG_000001.wav, ...)
/audio/manifest.csv                created by the device: one CRC-32 row per verified message
/audio/log.txt                     created by the device: boots, errors, power-cut recoveries
```

- **Safety:** the firmware never formats or overwrites anything, and message numbers are never reused.
- **Capacity:** a 180 s message is 5.76 MB, so 16 GB holds about 2 700 maximum-length messages.
- **Power cuts:** if power is lost mid-message, the next boot repairs the file and keeps everything up to the last 5 s checkpoint.

**Settings without reflashing:** edit `/audio/config.txt`. Out-of-range values are clamped, and every warning is written to `log.txt`. For lower energy, use:
- `standby_screen = minimal` or `off`
- a lower `led_brightness`
- a lower `oled_contrast`

## 6. Prepare the audio files

Playback accepts **only 16 000 Hz, mono, 16-bit PCM WAV**. Normalise speech to −18 LUFS; the volume setting assumes that level.

```sh
# greeting / instruction from any recording (m4a, mp3, wav, ...)
ffmpeg -i greeting_raw.m4a \
  -af "highpass=f=100,lowpass=f=7000,loudnorm=I=-18:TP=-1.5:LRA=11" \
  -ar 16000 -ac 1 -c:a pcm_s16le -map_metadata -1 -fflags +bitexact greeting.wav

# 400 ms, 1 kHz beep at -12 dBFS with 10 ms fades
ffmpeg -f lavfi -i "aevalsrc=0.25*sin(2*PI*1000*t):s=16000:d=0.4" \
  -af "afade=t=in:d=0.01,afade=t=out:st=0.39:d=0.01" \
  -ac 1 -c:a pcm_s16le -map_metadata -1 -fflags +bitexact beep.wav
```

Audacity alternative:
1. *Tracks → Mix → Mix Stereo Down to Mono*.
2. Set *Project Rate* to 16000.
3. *Effect → Loudness Normalization* to −18 LUFS.
4. *Export → WAV, Signed 16-bit PCM*.

Use ASCII file names exactly as listed above.

## 7. Hardware tests, then reliability tests

1. **Hardware tests:** flash `firmware/hwtest`, open the monitor (`idf.py -p PORT monitor`) and press `1`–`9`. Follow the stage order in [`docs/assembly.md`](docs/assembly.md). Send back every `RESULT T…` line plus the standby currents from test 9; they tune `config.txt` (volume, gain) and menuconfig (ring size, debounce). Details of each test: [`firmware/README.md`](firmware/README.md#phase-2-hardware-tests).
2. **Reliability tests:** flash `firmware/guestbook` and run [`docs/testing.md`](docs/testing.md). That covers the automated 100-session soak test, manual scenarios, fault injection (card removal, card full, power cuts) and energy measurements.

The test WAV files (`TEST_MIC.wav`, `TEST_DUPLEX.wav`, `TEST_SEQ.wav`) and the soak-test messages stay on the card so you can listen on a PC. **Use a separate test card**, or delete them plus `manifest.csv` before an event.

## 8. Event deployment checklist

1. **Firmware:** the normal `firmware/guestbook` build (never the soak build).
2. **Card:** FAT32, greeting files and `config.txt` present, no test files, at least 2 GB free.
3. **Power:** the 5 V ≥2 A **mains adapter** through the rear USB-C input only; no PC attached. Avoid power banks unless they have a low-current (always-on) mode, because standby draws only a few mA.
4. **Standby:** the OLED shows the total count and the LED is off.
5. **Test call:** lift the handset, hear the greeting and beep, speak 5 s, hang up. You should see "MESAJINIZ KAYDEDILDI!" and the count at the top-left goes up by one.
6. **Spare kit in the box:** pre-flashed Super Mini (guestbook build), second microSD card with greetings and `config.txt`, spare handset cord.
7. **After the event:**
   1. Power off and remove the card.
   2. Copy the whole `/audio/` folder (recordings, `manifest.csv`, `log.txt`) to two places.
   3. Spot-check messages against their manifest CRC (see `sdcard/README.md`).
   4. Only then reuse the card.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `export.sh`: Python virtual environment not found | Set `IDF_PYTHON_ENV_PATH` (§2) before sourcing `export.sh` |
| No serial port / flashing times out | BOOT + RESET sequence (§4); try another USB-C cable (some are charge-only) |
| Monitor silent after waking from deep sleep | Normal for a moment: USB re-enumerates on every wake; `idf.py monitor` reconnects |
| SD mount failed | Module VCC on 5 V; card FAT32 (not exFAT); run test 5 and check the mount clock it reports |
| Greeting rejected: "need 16000 Hz mono 16-bit PCM" | Re-export with the commands in §6 |
| Board resets when audio starts | Adapter too weak, bulk capacitors missing, or the rail's soft-start capacitor (470 nF on Q1's gate) missing (doc §6) |
| Boot banner says `SAFE MODE` | 3 crashes in a row: read the dump (`idf.py coredump-info`), check `/audio/log.txt`, try a fresh card |
| Menu prints `Peripheral rail: ON (leak!)` | A module kept the rail on after a test; report the test number |
| Phone switches off on a power bank | Standby current is below the bank's auto-off threshold: use the mains adapter or a bank with a low-current mode |
