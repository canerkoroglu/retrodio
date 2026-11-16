# microSD card template

Copy the `audio/` folder to the root of a FAT32-formatted card, then add your
`greeting.wav`, `instruction.wav` (optional) and `beep.wav` to `audio/greetings/`
(16 kHz, mono, 16-bit PCM; see the top-level README §6).

`audio/wifi.txt` holds the listening-mode network name and password (`SSID = …`,
`PASSWORD = …`, 8–63 characters, required; top-level README §9). Start from
`audio/wifi.example.txt`, copy it to the card as `/audio/wifi.txt` and put in your values.
`sdcard/audio/wifi.txt` is git-ignored, so a local copy with real values stays out of git.

The device creates these files itself:

| File | Content |
|---|---|
| `audio/recordings/MSG_000001.wav` … | guest messages, numbers never reused |
| `audio/manifest.csv` | one row per verified message: file, bytes, duration_ms, crc32 |
| `audio/log.txt` (`log.old`) | boot / error / recovery log, capped at 256 KB |

Verify a message after copying it to a PC (CRC-32 of the audio data after the 44-byte header):

```sh
python3 -c "import sys,zlib; d=open(sys.argv[1],'rb').read()[44:]; print(f'{zlib.crc32(d):08x}')" MSG_000001.wav
```
