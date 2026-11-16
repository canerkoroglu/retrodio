# Changelog

## 1.0.0 (guestbook), unreleased: not yet run on hardware

- OLED shows only the action, in large letters (2×): AHIZEYI KALDIRIN (standby, greeting) → KONUSMAYA BASLAYIN (recording, small countdown top-right) → AHIZEYI KAPATIN (time limit, handset left up) → MESAJINIZ KAYDEDILDI!. The message count is rotated 90° at the top-left (`!` when the card is almost full). Shared layout `rg_oled_action_screen()` is used by the guestbook and hwtest T2/T9.
- Fixes from code review:
  - Greeting/instruction longer than ~10 s no longer trips the task watchdog (reboot mid-greeting); playback feeds it.
  - A hang-up in the same tick as a recorder failure no longer leaves the device stuck on KAYDEDILIYOR.
  - Card filling up mid-message is detected (FatFs short write) and the truncated message is kept and counted.
  - An unreadable greeting/instruction file (truncated, renamed MP3) is skipped instead of failing every session.
  - Power-cut recovery deletes header-only files even with `min_seconds = 0`; a kept file refreshes the deep-sleep count.
  - WAV probe can no longer loop forever on a damaged chunk size.
  - The cold-boot log line is written once per boot.
  - hwtest: T1 HOOK check waits for the 100 nF debounce cap; T7 is INFO (not PASS) without greeting files; T3 reports the brightness cap in effect; T4 counters reset per run; T5 always closes its file; T6 noise excludes DC; T8 lists only WAVs written and holds the rail across the sequential switch; RX-only audio holds amp DIN low.
- Guestbook application `firmware/guestbook`.
  - State machine: INITIALIZING, WAKING, GREETING, INSTRUCTION, BEEP, RECORDING, MAX_DURATION, SAVING, SUCCESS, ERROR, HANG_UP, PREPARING_SLEEP. Standby is deep sleep.
  - Audio session engine: one I²S duplex session per guest. The file is created before the greeting and deleted on an early hang-up or a message shorter than `min_seconds`.
  - The message is finalized immediately at the time limit.
  - OLED UI service task and LED states per spec.
  - Card errors, card full, and SD timeouts (reboot) are handled.
- Recorder: CRC over confirmed writes only; salvages a verified, truncated message when the card fills mid-message; `rg_rec_discard()`.
- `config.txt`: `min_seconds`.
- Automated soak test (`sdkconfig.soak`): 100 simulated sessions through the real code path, outcome-checked.
- Shared `sdkconfig.common` / `partitions.csv` for both firmware projects.
- Docs: `architecture.md` (state machine), `assembly.md` (staged electronics build), `testing.md` (Phase 5).

## 0.3.0 (hwtest), unreleased

Energy:
- Switched 5 V peripheral rail (P-MOSFET on GPIO10 `PWR_EN`): SD module, amplifier, LED and
  mic are unpowered in deep sleep. Replaces the amplifier `SD_MODE` control.
- CPU at 80 MHz (≈6 mA less while awake than at 160 MHz).
- Standby screen variants (`full` / `minimal` / `off`) selectable in `config.txt` and
- NVS writes: one per cold boot and three per saved message (in-progress marker set and
  cleared, next index); deep-sleep wakes use an RTC cache instead of scanning the card.

Stability and redundancy:
- Recorder: stop deadline (`RG_ERR_TIMEOUT`), task watchdog on the SD writer (panic -> reboot),
  CRC-32 of every message, in-progress marker in NVS with header repair on the next boot.
- OLED: I2C bus reset and re-configuration on transfer errors.
- Crash-loop guard (safe mode after 3 consecutive crashes), crash dumps kept in flash.
- Catalog: valid-message count, never-reused numbering (NVS), `manifest.csv`, low-space minutes.
- Event log on the card (`log.txt`, rotated at 256 KB).
- `config.txt` on the card for volume, gain, duration, LED, contrast, standby screen.

Code quality:
- Project error codes (`rg_err.h`), `-Wall -Wextra -Werror` for project code.
- Host unit tests (`firmware/test/host`), `.clang-format`, `.editorconfig`, GitHub Actions
  (host tests, format check, ESP-IDF build).

## 0.2.0 (hwtest)

- Phase 2 hardware test firmware: tests 1–9, shared `rg` component.
