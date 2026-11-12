# LGAL-Doser – build guide

*English · [Deutsch](de/nachbau.md)*

This guide describes how to build the dosing pump yourself: parts, electrical safety, flashing
the firmware, start-up and test. It is meant for makers who can solder, measure with a
multimeter and start a Python script on the command line.

> **Status:** circuit board v1.0 (11/2026) is designed, the firmware is complete. Testing the
> firmware on the real hardware is still pending; board and enclosure files follow afterwards in
> [`hardware/`](../hardware/). Values that are only fixed with the hardware release are marked
> **open** below.

> **Dosing errors can kill animals.** Test a self-built device extensively without animals first
> (section [Test run](#8-test-run-without-animals)). Building and operating it is at your own
> risk, see [Safety and liability](../README.md#safety-and-liability).

## Contents

1. [Parts list](#1-parts-list)
2. [Tools](#2-tools)
3. [Electrical safety](#3-electrical-safety)
4. [Building and checking the board](#4-building-and-checking-the-board)
5. [Flashing and provisioning the firmware](#5-flashing-and-provisioning-the-firmware)
6. [Assembly](#6-assembly)
7. [Start-up and test](#7-start-up-and-test)
8. [Test run without animals](#8-test-run-without-animals)

## 1. Parts list

| Item | Qty | Part | Note |
|---|---|---|---|
| 1 | 1 | Circuit board LGAL-Doser v1.0 | manufacturing data in [`hardware/pcb/`](../hardware/pcb/) (to follow) |
| 2 | 1 | Waveshare ESP32-S3-DEV-KIT-N16R8 | 16 MB flash, 8 MB PSRAM; plugged into socket headers. For the variants N8R8 and N32R8 see the [technical docs](technical.md#module-variants) |
| 3 | 5 | Kamoer KPHM100-ST | peristaltic pump with stepper motor, 24 V, 1.0 A, 200 full steps/rev, no gearbox |
| 4 | 5 | TMC2209 StepStick (e.g. Ulegqin V2.0) | with heat sink; check the sense resistor (see [below](#tmc2209-drivers)) |
| 5 | 1 | USR-ES1 (W5500) | Ethernet over SPI |
| 6 | 1 | DS3231 module with AT24C32 EEPROM | clock (I²C 0x68) and memory (0x57) |
| 7 | 1 | Backup battery for the DS3231 module | LIR2032 (rechargeable) or CR2032 – a CR2032 **only without charging diode**, see [safety](#3-electrical-safety) |
| 8 | 1 | RGB light Mentor 2660.8302 | common anode |
| 9 | 1 | Button Multimec 3F | setup and factory reset |
| 10 | 1 | Switching regulator RECOM R-78E5.0 | 24 V → 5 V for the ESP board |
| 11 | 1 | Switching regulator RECOM R-78E3.3 | 24 V → 3.3 V for the logic |
| 12 | 5 | JST-XH connector 4-pin (socket + plug) | motor connection |
| 13 | 5 | M8 panel socket 3-pin | reserved for later extensions (3.3 V / signal / GND), not used by the firmware |
| 14 | 1 | Power supply 24 V DC, safety extra-low voltage (SELV) | rating and connector **open** (follow with the board's bill of materials) |
| 15 | 1 | Fuse in the 24 V supply line | value and type **open** (follow with the board's bill of materials) |
| 16 | 1 | Enclosure | print files in [`hardware/enclosure/`](../hardware/enclosure/) (to follow) |
| 17 | – | Spare pump tube BPT 3×5 mm, dosing tubes, check valves, holders at the tank | check valve or tube end above the water level |
| 18 | – | Passive parts, transistors, socket headers | according to the board's bill of materials (1 kΩ for the driver UART, 10 kΩ and 4.7 kΩ pull-ups, 100 nF at the button, NPN transistors for the LED) |
| 19 | 1 | Sticker for the rating plate | device password and setup Wi-Fi name |

Pin assignment and circuit are fixed, see [CLAUDE.md](../CLAUDE.md).

## 2. Tools

- Soldering station, solder, desoldering braid
- Multimeter (voltage, continuity)
- USB cable for the ESP board and a computer with Python 3 (for `esptool` and the scripts); to
  build the firmware yourself, ESP-IDF v6.0.3 (see [technical docs](technical.md#toolchain))
- Measuring cup or syringe with 0.1 ml graduation for calibration
- 3D printer for the enclosure (or your own enclosure)

## 3. Electrical safety

The dosing pump itself only works with **24 V DC**. The danger lies in the combination of power
supply, water and salt:

- **Power supply:** use only a finished, certified power supply with CE marking and safety
  extra-low voltage (SELV), short-circuit and overload proof. Never build or open the mains side
  (230 V) yourself.
- **Fusing:** in addition to the protection in the power supply, a fuse belongs in the 24 V
  supply line of the device, so that a fault on the board (e.g. a defective driver) cannot start
  a fire. Value and type are set by the board's bill of materials (**open**). Size the wires for
  the current.
- **Residual current device:** plug the power supply into a socket protected by an RCD (30 mA) –
  required at an aquarium for all devices anyway.
- **Water:** place device and power supply above the water level and out of splash water, lay
  cables with a drip loop, mind salt creep. Do not route tubes over the device.
- **Never plug or unplug motors under power.** A TMC2209 can be destroyed instantly. Unplug the
  power supply first.
- **StepSticks the right way round** (mind the GND/VM and EN labels); the wrong way round
  destroys them and can damage the board.
- **Backup battery:** many DS3231 modules charge the battery through a diode and are meant for a
  rechargeable LIR2032. A non-rechargeable **CR2032 may only be used without this charging
  diode**, otherwise it can leak or burst.
- **Measure first, then fit:** see the next section.

## 4. Building and checking the board

1. Assemble the board according to the bill of materials and placement plan (to follow in
   [`hardware/pcb/`](../hardware/pcb/)). Do **not** plug in the ESP board, StepSticks, Ethernet
   and RTC module yet.
2. **Visual check:** solder bridges, cold joints, polarity of the parts.
3. **Measure without power:** no short between 24 V and GND, 5 V and GND, 3.3 V and GND.
4. **First power without modules:** apply 24 V (ideally from a lab supply with current limit) and
   measure 5 V and 3.3 V at the socket headers.
5. Disconnect the power again.

### TMC2209 drivers

- The drivers are configured only over UART. No potentiometer or VREF needs to be set, and there
  are no jumpers for microsteps: the addresses 0–3 (bus A) and 0 (bus B) are hard-wired on the
  board.
- **Check the sense resistor:** the firmware computes the motor currents with 0.11 Ω (usual for
  these modules, usually marked `R110`). If another value is printed there, the currents are
  wrong – then open an issue before the first motor run.
- Stick on the heat sinks.

## 5. Flashing and provisioning the firmware

Inside the finished device the ESP board cannot be reached via USB. The first firmware is
therefore flashed **before the board is plugged in**; after that all updates run over the
network.

Every device needs its own secret, from which it derives the device password and the engineering
key. There are two ways:

**A – with your own manufacturer master (recommended if you build several devices):**

1. Once, create a file `factory.secret` with a long random line in the project folder and keep
   it safe:

   ```bash
   python3 -c "import secrets; print(secrets.token_hex(32))" > factory.secret
   ```

2. Flash the firmware – from a release the complete file matching the board and module
   (`d5v1` = LGAL-Doser 5, circuit board v1.0):

   ```bash
   python -m esptool --chip esp32s3 write-flash 0x0 lgal_doser-d5v1-N16R8-full.bin
   ```

   or built yourself with `idf.py -p /dev/cu.usbmodem* flash`.

3. Provision (writes the device secret and shows the values for the rating plate):

   ```bash
   python3 tools/provision.py -p /dev/cu.usbmodem*
   ```

   With fewer pumps built in, add e.g. `--pumps 1,2,3`: the device then comes up with exactly
   these pumps, also after a factory reset.

   The values can be computed again from the MAC address at any time
   (`tools/ap_password.py <MAC> --engineering`).

**B – without a master (single device):** at first start the device creates a random secret
itself and shows **device password and engineering key exactly once** on the serial console. So
that this output is not lost, do not start right after flashing, but open the monitor first:

```bash
python -m esptool --chip esp32s3 --after no-reset write-flash 0x0 lgal_doser-d5v1-N16R8-full.bin
```

```bash
idf.py -p /dev/cu.usbmodem* monitor
```

The monitor restarts the board, the values appear at the first start. Write both down at once:
nobody can compute them again. Provisioning by way A replaces them later.

Afterwards: write the device password and setup Wi-Fi name (`LG-Aquatic-Life-Setup-XXXX`) on the
**rating plate** and stick it into the enclosure. Keep the engineering key separately; it does
not belong on the rating plate.

Details and background: [technical docs, first start-up](technical.md#first-start-up-once-over-usb-before-installation).

## 6. Assembly

1. Power off. Plug in ESP board, StepSticks (the right way round, with heat sink), Ethernet and
   RTC module (with battery).
2. Fit board, pumps, button and LED into the enclosure.
3. **Connect the pumps:** seen from the front, pump 1 is the left one; the numbers on enclosure
   and board must match.
4. Stick in the rating plate, plug in the LAN cable, the power supply last.

## 7. Start-up and test

1. **Start:** the LED fades in white and then pulses orange (setup Wi-Fi on).
2. **Set up** as described in the [user guide](user-guide.md#3-initial-setup).
3. **Open the engineering menu:** in the web UI click the firmware version in the footer 7×
   quickly and enter the engineering key.
4. **Fitting** (area A): if fewer than five pumps are built in and provisioning had no `--pumps`,
   deselect the missing ones. The device restarts afterwards.
5. **Check the drivers** (area D): all fitted drivers must answer, without fault flags (short
   circuit, over-temperature, open coil).
6. **Run the pumps one by one** (area D, raw test): a few revolutions forward. If a pump runs the
   wrong way round, switch the **direction of rotation** in the pro view under pump → settings
   and check it with "Fill tube".
7. **Clock:** settings → time shows the source "time server" and the clock module without a
   warning.
8. **Calibrate all pumps**, see the [user guide](user-guide.md#64-calibrating).

## 8. Test run without animals

Before the device goes to a tank with animals:

1. Containers with water, tube ends in measuring cups.
2. Set a schedule and a daily limit for every pump and let it run for at least 24 hours.
3. Measure the delivered amounts and compare them with the history; deviations above a few
   percent → calibrate again, check the tube.
4. Simulate a power cut (unplug the power supply, plug it in again after a few minutes) and check
   that the device behaves as configured (skip or catch up missed doses) and that the time is
   right.
5. Only then start with the real solutions and initially reduced amounts, and test the water
   more often in the first days.

Please report problems with the build or the firmware as an issue; security vulnerabilities not
publicly, but as described in [SECURITY.md](../SECURITY.md).
