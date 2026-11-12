# LGAL Doser – firmware project

Dosing system for aquariums ("LG Aquatic Life"). First device: board v1.0 – 11/2026 with five pumps.
This file describes the **finished hardware**. Pin assignment and circuit are fixed and must
not be questioned by the firmware. If something is unclear, ask – do not guess.

---

## 1. Hardware overview

| Assembly | Part | Connection |
|---|---|---|
| MCU | Waveshare ESP32-S3-DEV-KIT-N16R8 (16 MB flash, 8 MB octal PSRAM) | plugged into socket headers |
| Pumps | 5× Kamoer KPHM100-ST (stepper motor, 24 V, no gearbox) | JST-XH 4-pin |
| Drivers | 5× TMC2209 StepStick (Ulegqin V2.0) | STEP individual, UART bus, EN shared |
| Ethernet | USR-ES1 (W5500) | SPI |
| Clock | DS3231 module (with AT24C32 EEPROM) | I²C |
| M8 sockets | 5× 3-wire (3.3 V / signal / GND), reserved | GPIO |
| Status | RGB light Mentor 2660.8302, common anode | 3× NPN low side |
| Button | Multimec 3F (reset/setup) | GPIO0 |
| Supply | 24 V external → R-78E5.0 (5 V) → ESP; R-78E3.3 (3.3 V) → logic | |

**Important:** Inside the enclosure the ESP board is **not reachable via USB**. The first firmware
is flashed on the computer before the board is plugged in; after that all updates run **over
the air (OTA)**. OTA must work reliably from the start (partition scheme with two OTA slots,
rollback on a failed start).

---

## 2. ESP32-S3 pin assignment

| GPIO | Function | Direction | Note |
|---|---|---|---|
| 40 | STEP pump 1 (driver U4) | out | |
| 41 | STEP pump 2 (U5) | out | |
| 42 | STEP pump 3 (U6) | out | |
| 2 | STEP pump 4 (U7) | out | |
| 1 | STEP pump 5 (U8) | out | |
| 21 | EN of all drivers | out | **active low**, external 10 kΩ pull-up → drivers OFF after reset |
| 38 | UART bus A (pumps 1–4) | TX+RX | single wire, 1 kΩ in series, see section 3 |
| 39 | UART bus B (pump 5) | TX+RX | single wire, 1 kΩ in series |
| 11 | ETH MOSI | out | W5500 |
| 12 | ETH SCLK | out | |
| 13 | ETH MISO | in | |
| 10 | ETH CS | out | |
| 9 | ETH INT | in | |
| – | ETH RST | – | 10 kΩ pull-up only, **no GPIO** (W5500 reset by software) |
| 8 | I²C SDA | bi | 4.7 kΩ pull-up on the board |
| 14 | I²C SCL | out | 4.7 kΩ pull-up |
| 48 | LED red | out | HIGH = on (NPN), PWM possible |
| 47 | LED green | out | HIGH = on |
| 45 | LED blue | out | HIGH = on. **Strapping pin**, do not drive during boot |
| 0 | Button | in | active low, 100 nF debounce. **Strapping pin** (pressed at power-on = download mode) |
| 7 | M8 socket 1 | – | reserved, not used by the firmware; **no external pull-up** |
| 15 | M8 socket 2 | – | reserved |
| 16 | M8 socket 3 | – | reserved |
| 17 | M8 socket 4 | – | reserved |
| 18 | M8 socket 5 | – | reserved |
| 4, 5, 6 | free | – | |
| 3, 46 | free | – | strapping pins, limited use only |
| 35–37 | taken | – | octal PSRAM, do not use |

Pump numbering = labels on enclosure and board: seen from the front, pump 1 is on the left.

---

## 3. TMC2209 stepper drivers

- **UART mode**, configuration through registers only (no potentiometer, no VREF).
- **Addresses** (hard-wired via MS1/MS2):

| Pump | Driver | UART bus | Address |
|---|---|---|---|
| 1 | U4 | A (GPIO 38) | 0 |
| 2 | U5 | A | 1 |
| 3 | U6 | A | 2 |
| 4 | U7 | A | 3 |
| 5 | U8 | B (GPIO 39) | 0 |

- **Single-wire UART:** each bus has only one GPIO, connected through 1 kΩ to PDN_UART of all
  drivers on that bus. TX and RX must be put on **the same GPIO** (half duplex). Sent bytes come
  back as an echo and must be discarded. Baud rate e.g. 115200.
- **DIR is hard-wired to GND.** The direction of rotation can only be switched through the
  register `GCONF.shaft`.
- **DIAG is not connected.** Stall detection (StallGuard) only by reading `SG_RESULT` /
  `DRV_STATUS` over UART while running.
- SPREAD, INDEX, CLK: open (internal clock, StealthChop by default).
- **Rsense** of the module: please check on the part (usually 0.11 Ω) – needed for the current
  calculation.
- Driver VIO = 3.3 V, VM = 24 V.

**Motor (Kamoer KPHM100-ST):** 2-phase, 1.0 A rated current, 2.5 Ω, 2.5 mH, holding torque
0.18 Nm, step angle 1.8° = 200 full steps/rev (checked 11/2026). Flow ≥ 80 ml/min at 300 rpm,
0–180 ml/min according to the data sheet, characteristic roughly linear up to 600 rpm.
→ Starting values: run current approx. 0.6–0.7 A RMS, holding current clearly reduced or drivers
switched off after dosing (EN), 16 microsteps with interpolation, StealthChop.
**ml per revolution must be calibrated per pump** (tube BPT 3×5 mm, tolerances).

All 5 drivers share EN. If several pumps run at the same time, each needs its own step clock.
Please check how many independent hardware clocks (RMT/MCPWM/LEDC) the ESP32-S3 offers for
5 channels, or run the pumps one after another.

---

## 4. Peripherals

- **Ethernet:** W5500, SPI on GPIO 10–13, INT GPIO 9, reset by software only. Ethernet is the
  main connection, Wi-Fi optional as a fallback. The antenna sits at the edge of the enclosure.
- **RTC:** DS3231 on I²C, address 0x68 (EEPROM AT24C32 at 0x57). Synchronise the time via NTP,
  RTC as the fallback without network. Backup battery LIR2032, or CR2032 without charging diode.
- **I²C expansion:** connector J16 (3.3 V, SDA, SCL, GND) for later extensions, e.g. a display.
- **M8 sockets:** 5× 3-wire (3.3 V / signal / GND) on GPIO 7, 15–18, reserved for later
  extensions. The firmware does not use them and leaves the pins unconfigured.
- **RGB light:** common anode on 5 V, the channels are switched to GND through NPN transistors:
  GPIO HIGH = colour on. PWM for mixed colours and brightness.
- **Button (GPIO 0):** hold 3 s = setup mode (access point), hold 10 s = factory reset. Pressed at
  power-on = ESP download mode (hardware, not firmware).
