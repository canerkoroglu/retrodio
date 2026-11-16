# Retro Audio Guestbook: firmware architecture (`firmware/guestbook`)

ESP-IDF v6.1.1, ESP32-C3. Hardware, pins and power: [`phase1-hardware-validation.md`](phase1-hardware-validation.md).

## 1. State machine

Every session starts from a deep-sleep wake, which is a fresh boot. No state leaks from one guest to the next. Standby *is* deep sleep: the OLED keeps the last standby frame and the peripheral rail is off.

```mermaid
stateDiagram-v2
    [*] --> INITIALIZING: power-on / reset
    [*] --> WAKING: deep-sleep wake (handset lifted)
    [*] --> ERROR: safe mode (3 crashes in a row)

    INITIALIZING --> HANG_UP: handset lifted after a crash / software reset
    INITIALIZING --> WIFI: handset lifted at power-on / RESET
    INITIALIZING --> PREPARING_SLEEP: card read, handset down
    WIFI --> [*]: Kapat on the page, or 5 min idle -> reboot
    INITIALIZING --> ERROR: card error

    WAKING --> PREPARING_SLEEP: false wake (not lifted)
    WAKING --> GREETING: card OK, file created
    WAKING --> ERROR: card error / card full

    GREETING --> INSTRUCTION: greeting played
    INSTRUCTION --> BEEP: instruction played
    BEEP --> RECORDING: beep fully played
    GREETING --> PREPARING_SLEEP: hang-up (file deleted)
    INSTRUCTION --> PREPARING_SLEEP: hang-up (file deleted)
    BEEP --> PREPARING_SLEEP: hang-up (file deleted)

    RECORDING --> SAVING: hang-up
    RECORDING --> MAX_DURATION: limit reached (file finalized at once)
    MAX_DURATION --> SUCCESS: hang-up, file verified
    MAX_DURATION --> ERROR: hang-up, save failed
    SAVING --> SUCCESS: file verified, counted
    SAVING --> PREPARING_SLEEP: shorter than min_seconds (deleted)
    SAVING --> ERROR: save failed

    SUCCESS --> PREPARING_SLEEP: after 2 s
    ERROR --> PREPARING_SLEEP: after 3 s and handset down
    ERROR --> [*]: SD timeout -> reboot
    HANG_UP --> PREPARING_SLEEP: handset down

    PREPARING_SLEEP --> WAKING: lifted again (after a hang-up)
    PREPARING_SLEEP --> HANG_UP: still lifted from the last session
    PREPARING_SLEEP --> [*]: down >= 500 ms -> deep sleep (STANDBY)
```

Every screen except ERROR uses one layout: the message count rotated 90° in the left column, vertically centred, with 21 px tall digits (`025`, `---` before the card is read). Four or more characters (`025!` when the card is almost full, or 1000+ messages) are drawn smaller. The action is in large letters (2×, up to 10 characters per line) beside it.

| State | OLED | WS2812B | Rail | Notes |
|---|---|---|---|---|
| STANDBY (deep sleep) | AHIZEYI / KALDIRIN (`full`), count only (`minimal`), or panel off | off | **off** | ESP32 + OLED only |
| WAKING | AHIZEYI / KALDIRIN | blue, short blink | on | lift must stay ≥ 60 ms; card mount, recovery, `config.txt`, catalog |
| GREETING | AHIZEYI / KALDIRIN | yellow, slow breathing | on | WAV file already created (SD latency can never cost the first words) |
| INSTRUCTION / BEEP | AHIZEYI / KALDIRIN | yellow, slow breathing | on | `beep.wav`, or a built-in 400 ms 1 kHz tone |
| RECORDING | KONUSMAYA / BASLAYIN, small 2:59 bottom-right | green, breathing | on | countdown each second |
| MAX_DURATION | AHIZEYI / KAPATIN | yellow, solid | on | message already finalized (decision D9) |
| SAVING | LUTFEN / BEKLEYIN | cyan, fast blink | on | header, fsync, close, re-open, verify |
| SUCCESS | MESAJINIZ / KAYDEDILDI | green, solid 2 s | on | count and manifest updated only now |
| ERROR | KAYIT HATASI · KART HATASI · KART DOLU · SERVIS GEREKLI · WIFI AYARI YOK + hint | red, fast blink | on | min 3 s, until hang-up |
| HANG_UP | AHIZEYI / KAPATIN | off | on | crash / software reset or session end with the handset still up |
| WIFI (listening mode) | WIFI *ssid* / 192.168.4.1 / CALIYOR: MESAJ 12 | blue, slow breathing | on | access point + web player, no guest sessions (§8) |

Rules from the spec, and where they are enforced:

| Rule | Enforced in |
|---|---|
| Hang-up during greeting, instruction or beep: no recording | `gb_audio.c` deletes the pre-created file (`rg_rec_discard`) |
| Never report success before finalization | `gb_fsm.c` `on_saved()` only after `rg_rec_stop()` returns ESP_OK (or a verified salvaged file) |
| Recording starts only after the beep has finished | `rg_audio_drain()`, then `rg_audio_rx_flush()` |
| No deep sleep while lifted | PREPARING_SLEEP requires ≥ 500 ms debounced "down" |
| No session started by a cold boot | INITIALIZING never starts a guest session |
| Wi-Fi only on purpose | WIFI is entered only from INITIALIZING after a power-on / RESET with the handset lifted, never after a crash or watchdog reset |
| Never an empty file | 0 samples, or fewer than `min_seconds`, are deleted |

## 2. Tasks

| Task | Prio | Stack | Job |
|---|---|---|---|
| `gb_audio` | 10 | 6 KB | I²S duplex: intro playback, capture, int16 conversion, pushes into the ring buffer |
| `rg_rec` (SD writer) | 5 | 4 KB | ring buffer → 4 KB SD writes, checkpoint every 5 s, CRC-32 |
| `gb_ui` | 3 | 3 KB | renders the UI model (≈13 ms I²C flush) |
| `rg_led` | 2 | 2.5 KB | 50 Hz effect rendering |
| main (FSM) | 1 | 10 KB | 10 ms tick: hook debounce, audio events, state transitions |
| `gb_player` (listening mode only) | 6 | 6 KB | plays the message chosen on the web page (I²S TX) |
| `httpd` (listening mode only) | 5 | 6 KB | web page, JSON API, file download |

How the tasks share work:
- **Capture never waits on the SD card or the display.** The audio task is highest priority and only does non-blocking pushes into a 64 KB ring (≈2 s of audio).
- **Shared resources are each owned once.** I²S is owned by `gb_audio`, the SD file by `rg_rec`, the frame buffer by `gb_ui` (mutex), and the rail by `rg_power` (reference count).
- **Messages flow through FreeRTOS primitives.** The FSM talks to the audio task through a command queue (one session) plus a hang-up flag; the audio task answers through an event queue. The audio and SD writer tasks pass data through a single-producer single-consumer stream buffer.

## 3. Audio data path

```mermaid
flowchart LR
    SD[(greeting / instruction / beep)] --> PLAY[rg_audio_play_wav<br>volume Q15] --> TX[I2S TX DMA 128 ms] --> AMP[MAX98357A]
    MIC[I2S mic] --> RX[I2S RX DMA 128 ms] --> CONV[rg_mic_conv<br>gain, DC block, saturate] --> RING[(64 KB ring)] --> WR[rg_rec writer<br>4 KB writes, CRC-32,<br>header+fsync / 5 s] --> FILE[(MSG_xxxxxx.wav)]
```

- I²S: 16 kHz, Philips, 2×32-bit slots, BCLK 1.024 MHz, MCLK 512·fs. TX and RX are paired on I2S0 (shared BCLK/WS).
- The mic warms up (2¹⁸ SCK ≈ 256 ms) during the greeting.
- At the limit or on hang-up, the clocks stop and the pins float before the rail drops (`rg_audio_close` → `rg_power_release`).

## 4. Timing (all configurable)

| Setting | Default | Where |
|---|---|---|
| Hook debounce | 40 ms | menuconfig `RG_HOOK_DEBOUNCE_MS` |
| Lift confirmation after wake | 60 ms | `GB_WAKE_CONFIRM_MS` |
| Down before deep sleep | 500 ms | `GB_SLEEP_STABLE_MS` |
| Success screen | 2000 ms | `GB_SUCCESS_SCREEN_MS` |
| Error screen (minimum) | 3000 ms | `GB_ERROR_SCREEN_MS` |
| Save deadline → reboot | 5000 ms | `GB_SAVE_TIMEOUT_MS` (< task watchdog 10 s) |
| Ring buffer | 64 KB | `GB_RING_KB` (size from hardware test 5) |
| Rail settle | 30 ms | `RG_POWER_SETTLE_MS` |
| Max / min message, volume, gain, LED, contrast, standby screen, low-space warning | 180 s / 1 s / −6 dB / 12 dB / 40 / 0x30 / full / 30 min | `/audio/config.txt` |

## 5. Failure handling

| Failure | Behaviour |
|---|---|
| No card / unreadable | KART HATASI. Nothing recorded. Back to sleep after hang-up; the next lift retries. |
| Card full before a session | KART DOLU (needs ≥ 1 MB + 10 s of audio free). |
| Card full during a message | The recorder truncates to the last complete chunk and verifies it. The message is kept and counted; the standby count gets a `!` (e.g. `025!`). |
| SD write error | KAYIT HATASI. The file stays marked; the next boot repairs it if possible. |
| SD stops responding | The 5 s save deadline expires: error screen, then reboot. A watchdog panic after 10 s covers a fully stuck writer. The file is repaired on the next mount. |
| Power cut while recording | The next boot repairs the header from the file length (≤ 5 s lost), or deletes it if shorter than `min_seconds`. |
| Repeated crashes | After 3 in a row: SERVIS GEREKLI, card not touched, until power-cycled. The first crash dump stays in flash. |
| Bad greeting format | Skipped with a log warning; the guest still gets the beep and can record. |
| Ring buffer overflow | Counted as dropped bytes, logged with "(audio dropped: enlarge ring)". |

## 6. Energy

- **Deep sleep between every guest:** only the ESP32 + OLED are powered (rail off).
- **CPU at 80 MHz.**
- **Card writes during sessions:** the event log is written only on cold boot and on events (saved, failed, recovery, card full). It is never written on a plain wake or while recording.
- **NVS writes:** one per cold boot, three per message.
- **No folder scan on wake:** the catalog uses an RTC cache that is invalidated by a different card (CID).
- **Rail off as early as possible:** right after the result screen.

## 7. Soak test (Phase 5)

`sdkconfig.soak` builds a variant that simulates the handset and wakes by timer. It runs 100 complete sessions through the real code path, including deep sleep, and checks each outcome against a script:

| Session type (per 100 sessions) | Count | Expected outcome |
|---|---|---|
| normal 4 s message | 78 | saved |
| hang-up 200 ms into the greeting / beep | 10 | discarded |
| too-short message (hang-up 0.3 s after the beep) | 8 | discarded |
| maximum-length message (limit 10 s, hang-up 2 s later) | 4 | saved |

It ends with `RESULT SOAK PASS|FAIL sessions=… saved=… discarded=… errors=… mismatches=…`, which goes to the console and to `log.txt`. Procedure: [`testing.md`](testing.md).

## 8. Listening mode (Wi-Fi)

Power the phone on (plug in, or press RESET) **with the handset lifted**. Instead of AHIZEYI KAPATIN, the WIFI state starts (`gb_web.c`):

- **Access point:** network name and password come from `/audio/wifi.txt` (`SSID = …`, `PASSWORD = …`, template `sdcard/audio/wifi.example.txt`). The password is required. Without a usable file the mode does not start and the OLED shows WIFI AYARI YOK (ERROR state, back to standby after hang-up); an optional menuconfig fallback lives only in the git-ignored `sdkconfig`. WPA2, channel 6, at most 4 phones, transmit power capped at 8.5 dBm (Super Mini antenna).
- **Web page:** `http://192.168.4.1` lists the messages, newest first. *Çal* plays one through the handset speaker, *Durdur* stops it, *İndir* downloads the WAV file and *Kapat* ends the mode.
- **No guest sessions** while the mode is on. The guest engine stays idle, so the player owns I²S TX (BCLK and LRC start and stop together, as in a session).
- **The mode ends** after 5 idle minutes (handset down, nothing playing, no request from the page; `GB_WIFI_IDLE_MIN`) or with *Kapat*. It ends with a reboot, which releases all Wi-Fi memory. A software reset never re-enters the mode, so with the handset still up the phone shows AHIZEYI KAPATIN.

| Endpoint | Method | Result |
|---|---|---|
| `/` | GET | the page |
| `/api/list` | GET | `[[index, duration_ms], …]`, newest first |
| `/api/status` | GET | `{"playing": index or 0, "pos": ms, "len": ms}` |
| `/api/play?n=12` | POST | play `MSG_000012.wav` (a newer request interrupts the current one) |
| `/api/stop` | POST | stop playback |
| `/api/exit` | POST | leave listening mode (reboot) |
| `/rec/MSG_000012.wav` | GET | download the file |

Only names that parse as `MSG_nnnnnn.wav` are served, so no other file on the card can be read.
