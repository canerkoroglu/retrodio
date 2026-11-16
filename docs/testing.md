# End-to-end testing (Phase 5)

Run this after the hardware tests 1–9 pass ([`assembly.md`](assembly.md) stage 9) and the guestbook firmware is flashed. Use a **test card**: every run writes real `MSG_*.wav` files.

Record every result in the table at the end. The device logs to `/audio/log.txt`; copy it off the card after each block.

## A. Automated soak test: 100 consecutive sessions

Build and flash the soak variant:

```sh
cd firmware/guestbook
idf.py -B build-soak -D SDKCONFIG=build-soak/sdkconfig \
       -D SDKCONFIG_DEFAULTS="../sdkconfig.common;sdkconfig.soak" build
idf.py -B build-soak -p PORT flash monitor
```

Setup and run:
- Greeting files on the card. Handset on the cradle (the hook is simulated; a lifted handset would wake the board).
- Power through the USB power meter.
- Runtime ≈ 100 × (greeting + ~6 s) + sleep gaps.

Pass when:
- [ ] The console ends with `RESULT SOAK PASS sessions=100 … errors=0 mismatches=0`.
- [ ] `log.txt` has 82 `saved` lines and no `failed` lines.
- [ ] `manifest.csv` has 82 rows; spot-check 5 CRCs (`sdcard/README.md`).
- [ ] The standby count on the OLED matches `manifest.csv`.
- [ ] No `crash dump stored` in the boot banner afterwards.

Then reflash the **normal** build: `idf.py -p PORT flash` in `firmware/guestbook` (without `-B build-soak`).

## B. Manual scenarios (normal build)

| # | Scenario | Steps | Expected |
|---|---|---|---|
| B1 | Normal message | Lift, listen, speak 10 s after the beep, hang up | Greeting, instruction, beep; KONUŞMAYA BAŞLAYIN with countdown from 3:00; LÜTFEN BEKLEYİN, MESAJINIZ KAYDEDİLDİ, count +1; green 5 s; sleep |
| B2 | Hang-up during greeting | Lift, hang up after 2 s | No file, count unchanged, back to standby |
| B3 | Hang-up during beep | Hang up while the beep sounds | No file, count unchanged |
| B4 | Too short | Hang up < 1 s after the beep | File deleted, count unchanged |
| B5 | Maximum duration | Set `max_seconds = 20` in `config.txt`, keep talking | At 0:00: AHİZEYİ KAPATIN, yellow; file already saved; hang up → MESAJINIZ KAYDEDİLDİ |
| B6 | Lift again during the result screen | Hang up, lift again while MESAJINIZ KAYDEDİLDİ shows | The result screen ends at once and a new session starts (greeting without delay) |
| B7 | Never hung up | After B5, leave the handset off for 5 min | Stays on AHİZEYİ KAPATIN and never sleeps; hang up → result |
| B8 | Cold boot with handset lifted | Power on with the handset off | Listening mode (B11), never a greeting or recording |
| B9 | Bounce / quick tap | Tap the hook switch quickly 10× | No session starts, no stray files (false wakes go back to sleep) |
| B10 | Playback quality | Copy B1's file to a PC | 16 kHz mono 16-bit, intelligible, no clicks or hum; level peaks around −12…−3 dBFS |
| B11 | Listening mode | Lift, power on; join the `wifi.txt` network; open `http://192.168.4.1` | OLED `WIFI <ssid>`, `192.168.4.1`, blue LED; page lists every message newest first; Çal plays it in the handset with no pop at start or end; Durdur stops; İndir saves a WAV identical to the card's |
| B12 | Listening mode exit | Tap Kapat; repeat B11 and instead leave the handset down 5 min | Both reboot into normal standby; a lift afterwards records normally |
| B13 | Listening mode credentials | Change `wifi.txt` to another SSID, repeat B11; then set a 7-character password; then delete `wifi.txt` | New SSID used; short password and missing file: WIFI.TXT YOK, no network, standby after hang-up; `log.txt` lists the reason |

## C. Fault injection

| # | Fault | Steps | Expected |
|---|---|---|---|
| C1 | No card | Remove the card, lift | KART HATASI, red; nothing recorded; sleeps after hang-up |
| C2 | Card removed while recording | Pull the card mid-message | KAYIT HATASI; with the card back, the next boot repairs or deletes the file (`log.txt`: power-cut recovery) |
| C3 | Card full before a session | Fill the card until < 1 MB + 10 s free, lift | KART DOLU; the standby count shows `!` (e.g. `025!`) |
| C4 | Card fills during a message | Leave ~30 s free, record 60 s | Message kept (truncated), counted, `log.txt` "(card full)"; next lift: KART DOLU |
| C5 | Power cut while recording | Unplug 20 s into a message, replug | Next boot: file repaired, ≤ 5 s lost, counted on the next cold boot |
| C6 | Power cut while saving | Unplug during LÜTFEN BEKLEYİN | File either complete or repaired on boot; never a half-written header |
| C7 | Reboot / count recovery | Power-cycle 3× | Standby count unchanged; next message number continues (never reused) |
| C8 | Deleted files on a PC | Delete MSG_000002.wav on a PC, reinsert, power-cycle | Count drops by 1; the next number still continues past the highest ever used |
| C9 | Bad greeting format | Replace greeting.wav with a 44.1 kHz stereo file | Greeting skipped (warning on the serial console), beep still plays, recording works |
| C10 | Bad config.txt | `volume_db = 50`, `colour = red`, then power-cycle | Clamped to 0 dB, unknown key ignored; both warnings in `log.txt` (written at cold boot) |
| C11 | Crash-loop guard | Developer only: force 3 panics in a row | CİHAZ ARIZASI; card not touched until power-cycled; dump readable with `idf.py coredump-info` |

## D. Energy and endurance

| # | Measurement | Expected (estimate) |
|---|---|---|
| D1 | Standby current, `standby_screen = full` / `minimal` / `off` | ≈3–10 / 1–4 / ≈0.1 mA at 5 V |
| D2 | Active current during greeting and recording | ≈70–150 mA |
| D3 | Wake to greeting start (stopwatch from lift) | < 1 s |
| D4 | 12 h idle overnight, then B1 | Message saved, count correct |
| D5 | Event-length run: 50 manual or soak sessions over 4 h | No errors in `log.txt` |

## E. Results log

| Test | Date | Result | Notes |
|---|---|---|---|
| A soak | | | |
| B1–B10 | | | |
| C1–C11 | | | |
| D1–D5 | | | |
