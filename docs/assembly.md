# Hardware assembly guide (electronics)

Build the electronics in stages and test each stage before adding the next. The hardware-test firmware (`firmware/hwtest`) has a test for every stage, so a fault is always found in the part you just added.

- Connection details: [`phase1-hardware-validation.md`](phase1-hardware-validation.md) §3 (pins) and §4.1 (every connection).
- Parts list: §8.

The enclosure is your existing one; this guide covers the electronics and cabling only.

## 0. Before you start

**Tools:**
- soldering iron, flux, desoldering braid
- multimeter
- USB power meter (5 V, mA resolution)
- 3–5 cm protoboard pieces or one ≈70×50 mm board
- JST-XH crimp kit (or pre-crimped leads)

**Check the modules you have:**

| Check | How | Why |
|---|---|---|
| Super Mini red power LED | Desolder it (or cut its trace). | 1–3 mA in standby, always on |
| SD module signal path | Look for a 14-pin chip (74LVC125/74HC125). Resistors only? Then bridge the series/divider resistors on CS, SCK, MOSI. | Dividers drop the ESP's 3.3 V to ~2.2 V at the card |
| OLED pull-ups | Ohmmeter SDA→VCC and SCL→VCC on the unpowered module: 4.7–10 kΩ means present. | If missing, fit 4.7 kΩ to 3V3 |
| MAX98357A SD pin | SD→VIN ≈ 1 MΩ on the unpowered module (Adafruit-style). | Gives (L+R)/2 mode; leave SD unconnected |
| Mic | Note the markings on the can; check there is a hole under it (bottom port). | Test 6 confirms bit depth |
| Hook switch | Bare 3-terminal microswitch: identify COM / NO / NC with the ohmmeter. PCB endstop: desolder its LED, power it from 3.3 V. | NC wiring = 0 µA in standby, fails safe |

## 1. Board layout

```
             ┌────────────────────────── base board ──────────────────────────┐
 USB-C in ─► │ PTC ─ TVS ─ 1000µF ═══ 5V bus ═══╦═ Super Mini (on sockets)    │
 (rear)      │                                  ║    3V3 ─ OLED connector ────┼─► OLED window
             │                                  ╚═ Q1 rail switch ═ 5V_SW ═╗  │
             │   pull resistors near the Super Mini header                  ║  │
             │                                    ┌─ SD module (near card slot access)
             │                                    ├─ MAX98357A ─ SPK pair ───┼─► cord RJ45/JST
             │                                    ├─ 74AHCT1G125 ─ LED conn ─┼─► LED diffuser
             │                                    └─ U1 3.3 V LDO ─ 3V3_SW ──┼─► cord (mic VDD)
             │   SRV05-4 TVS at the cord connector                           │
 hook ◄──────┼── JST-2 (COM, NC)                                             │
             └───────────────────────────────────────────────────────────────┘
```

- Keep SD wires ≤ 10 cm, and I²S wires short on the board (the long part is the cord).
- One ground point: the 1000 µF bus capacitor. Run each module's GND back to it, not daisy-chained through other modules.
- Mount the Super Mini on sockets, so a spare board can be swapped in on site.

## 2. Rail switch (Q1, Q2, U1)

```
 5V bus ──┬──────────────┐
          │              │ S
        100kΩ   470nF   Q1 AO3401A (P-MOSFET)
          │       │      │ D
          ├───────┴──── G│
          │              └──────────────┬──► 5V_SW (SD, amp, shifter, LED)
         10kΩ                           │
          │ D                           └──► U1 XC6206P332 IN ─ OUT ──► 3V3_SW (mic)
 GPIO10 ─ G  Q2 2N7002 (N-MOSFET)            1µF        1µF
   │      │ S
 100kΩ   GND
   │
  GND
```

- **GPIO10 low or high-Z** (boot, deep sleep): Q2 is off, Q1's gate is pulled to 5 V, so the rail is **off**.
- **GPIO10 high:** Q2 pulls Q1's gate to ≈0.45 V, so the rail is **on**. The 470 nF capacitor makes the turn-on take about 5 ms (no brownout).

## 3. Stage-by-stage build and test

Each stage lists what to build, what to measure, and the hardware test to run. Flash `hwtest` once in stage 2 (top-level README §3–4) and keep it on the board until stage 9.

### Stage 1: power entry

**Build:** USB-C power-only breakout → PTC → 5V bus, with the TVS and 1000 µF + 100 nF across the bus.

**Check:**
- 5.0 V ±0.25 V at the bus with the adapter.
- Correct polarity: reverse leads on the meter read negative.

### Stage 2: Super Mini, pull resistors, hook switch

**Build:**
1. 5V bus → Super Mini **5V** pin. GND → GND.
2. The pull resistors:

| Resistor | Between |
|---|---|
| 100 kΩ | GPIO10 → GND |
| 100 kΩ | GPIO2 (CS) → 3V3 |
| 10 kΩ | GPIO3 → 3V3 |
| 100 kΩ | GPIO0 → GND |
| 100 kΩ | GPIO1 → GND |
| 100 kΩ | GPIO5 → GND |
| 10 kΩ | GPIO7 → GND |

3. 47 Ω in series on GPIO0 and GPIO1 (on the cord side of the 100 kΩ pull-downs).
4. Hook: COM → GND; NC → 1 kΩ → GPIO3; 100 nF from GPIO3 to GND.

**Check:**
- 3.3 V on the 3V3 pin.
- Handset on the cradle → GPIO3 = 3.3 V; handset lifted → 0 V.

**Test:** flash `hwtest`.
- **T1:** all expected pulls PASS (SDA/SCL show *floating* until the OLED is fitted; that is expected here).
- **T4:** bounce and polarity.

### Stage 3: OLED

**Build:** OLED VCC → 3V3, GND, SDA → GPIO8, SCL → GPIO9 (JST-4 to the display window).

**Test:**
- **T2:** all screens readable.
- **T1** again: SDA/SCL now pull-up.

### Stage 4: rail switch

**Build:** Q1, Q2, U1 and capacitors as in §2, with no loads on 5V_SW yet.

**Check:**
- At idle (menu shown) 5V_SW = 0 V and 3V3_SW = 0 V.
- During **T3** (it holds the rail): 5V_SW ≈ 4.9–5.0 V, 3V3_SW ≈ 3.3 V.
- After the test, the menu prints `Peripheral rail: off` and both rails read 0 V again.

### Stage 5: status LED

**Build:**
1. 74AHCT1G125: VCC → 5V_SW, GND, OE → GND, A → GPIO7, Y → 330 Ω → WS2812B DIN.
2. WS2812B VDD → 5V_SW, with 100 nF + 10 µF at the LED.

**Test:** **T3**. Colours match, dark at the end, brightness comfortable.

### Stage 6: microSD

**Build:**
1. SD module VCC → 5V_SW (47 µF + 100 nF at the module).
2. CS → GPIO2, SCK → GPIO4, MOSI → GPIO21, MISO → GPIO20.

**Test:** **T5**.
- Mounts at 20000 kHz.
- verify=ok.
- Note `ring_kb` and `max_write_ms`.

### Stage 7: amplifier and speaker

**Build:**
1. MAX98357A VIN → 5V_SW (100 µF + 10 µF + 100 nF), GND.
2. BCLK → GPIO0 (after the 47 Ω), LRC → GPIO1 (after the 47 Ω), DIN → GPIO6.
3. GAIN → 100 kΩ → VIN. SD **unconnected**.
4. Speaker via the cord pair (temporary short leads are fine for this stage).

**Test:** **T7**. Pick the comfortable tone step, no pops; note `suggested_volume_db`.

### Stage 8: handset, cord, microphone

**Build:**
1. Wire the cord (table below), the SRV05-4 at the base connector, and the mic in the handset.
2. Mic: VDD from 3V3_SW through a ferrite bead, with 10 µF + 100 nF at the mic. **L/R → GND.**
3. Strain-relief the cord at both ends.

**Test:**
- **T6:** slot LEFT, valid bits 24, speak-test level meter, play back `TEST_MIC.wav`.
- **T8:** both modes; listen to `TEST_DUPLEX.wav` on a PC for clicks or buzz from the idle amp in the cord.

**Handset cord (8-conductor, T568B colours):**

| RJ45 pin | Colour | Signal | Pair |
|---|---|---|---|
| 1 | white/orange | SCK (BCLK) | with 2 |
| 2 | orange | GND | |
| 3 | white/green | SD (mic data) | with 6 |
| 6 | green | GND | |
| 4 | blue | SPK+ | with 5 |
| 5 | white/blue | SPK− | |
| 7 | white/brown | WS | with 8 |
| 8 | brown | 3V3_SW (mic VDD) | |

If T6/T8 recordings show noise that appears only through the cord, see the fallback in Phase 1 doc §4.3.

### Stage 9: complete device

1. **T9:** run it three times (standby screen full, minimal, off). Note the 5 V standby current of each. Make 20+ hang-up/lift cycles with zero false wakes.
2. Paste all `RESULT` lines back. The values tune `config.txt` and menuconfig: volume, gain, ring size, debounce.
3. Flash the guestbook application (top-level README §4, `firmware/guestbook`).
4. Run the soak test and the checklist in [`testing.md`](testing.md).

## 4. Final checks before closing the enclosure

- [ ] No bare joints near the cord connector; heat-shrink on every splice.
- [ ] Cord strain relief holds a firm pull without load on the solder joints.
- [ ] microSD reachable through the maintenance access.
- [ ] Super Mini USB-C reachable for reflashing (or via a short internal extension).
- [ ] LED behind its diffuser, OLED aligned with its window.
- [ ] Spare kit packed: pre-flashed Super Mini, second card, spare cord.
