<p align="center"><img src="https://raw.githubusercontent.com/ssiergl/lgal-brand/main/logo/lgal-plaque.svg" width="200" alt="LG Aquatic Life"></p>

# LGAL-Doser

*English · [Deutsch](README.de.md)*

Open dosing pump for reef aquariums by **LG Aquatic Life**. The first device has five
peristaltic pumps with stepper motors, controlled by an ESP32-S3; versions with fewer pumps
are planned. It is operated through a web UI in the browser and, if you like, from Home
Assistant as well. Firmware, circuit board and enclosure are open in this repository.

> **Important:** A dosing pump adds substances to the aquarium that can kill animals in the
> wrong amount. Read the [safety notes](#safety-and-liability) before first use.

## Features

- **Five independent pumps:** Kamoer KPHM100-ST with TMC2209 drivers. The pumps run quietly in
  StealthChop mode with a soft ramp, and all of them can run at the same time.
- **Accurate dosing:** calibration wizard with a measuring cup, retract against dripping, tube
  priming by holding a button (stops at once on release or when the connection drops).
- **Schedules:** per pump a daily amount spread over N doses, or fixed times with their own
  amount; daily, on weekdays or every N days. Plus boost (extra amount over a few days) and
  lock times between pumps, e.g. no calcium right after alkalinity.
- **Protection against overdosing:** hard daily limit per pump, container level with range in
  days, all schedules checked on save, defined behaviour after a power cut.
- **Web UI** right on the device, no cloud and no app: overview of all pumps, history with
  charts, **simple** and **pro** view, dark and light theme, English and German.
- **Networked:** LAN as the main connection, Wi-Fi as fallback. Setup Wi-Fi with a setup
  wizard, time via NTP with a clock module as fallback.
- **Home Assistant / MQTT:** automatic discovery, sensors per pump, optional dosing and
  switching from Home Assistant, update entity. TLS with your own CA certificate.
- **Updates over the network** with automatic rollback if a new firmware does not start
  cleanly. Optionally the device checks for new versions daily.
- **Status LED** (red, yellow, green …) with night mode, one button for setup and factory reset.

## Hardware at a glance

| Assembly | Part |
|---|---|
| Controller | Waveshare ESP32-S3-DEV-KIT-N16R8 (firmware also for N8R8 and N32R8) |
| Pumps | 5× Kamoer KPHM100-ST, 24 V, tube BPT 3×5 mm |
| Motor drivers | 5× TMC2209 (StepStick), configured over UART |
| Network | USR-ES1 (W5500) for LAN, Wi-Fi in the ESP32-S3 |
| Clock | DS3231 with backup battery |
| Controls | RGB status light, button |
| Supply | 24 V external |

Pin assignment and circuit: [CLAUDE.md](CLAUDE.md).

## Documentation

| Document | For |
|---|---|
| [User guide](docs/user-guide.md) | owners: setup, calibrating pumps, schedules, daily use, troubleshooting |
| [Build guide](docs/build-guide.md) | makers: parts list, electrical safety, flashing the firmware, assembly and test |
| [Technical documentation](docs/technical.md) | developers: building the firmware, first USB flash, API, MQTT, internals |
| [Changelog](CHANGELOG.md) | what changed from version to version |
| [Security](SECURITY.md) | security model, known limits, reporting vulnerabilities privately |

German versions of the README and the guides are in [README.de.md](README.de.md) and
[docs/de/](docs/de/).

## Project status

The firmware is complete (milestones M0–M6) and tested with host tests, fuzzers and a device
mock. The circuit board (v1.0, 11/2026) is designed; testing the firmware on the real hardware
is still pending. Until then: **not yet suitable for use on an aquarium with animals.**

You can try the web UI in the browser without a device:

```bash
python3 tools/mock/webui_server.py
```

Then open `http://localhost:8767/`, password `test-admin-pw`. More in the
[technical documentation](docs/technical.md#mock-server-without-a-device).

## Repository

```
components/   firmware (ESP-IDF components) and web UI
main/         firmware start-up
tools/        provisioning, update upload, release build, device mock
tests/        host tests and fuzzers
docs/         documentation, German versions in docs/de/
hardware/     enclosure (STL …) and circuit board (schematic, Gerber …)
```

## Reporting bugs

Please open an issue with the "Bug report" template, in English or German. Include the firmware
version and, if possible, the log from the maintenance page – without passwords or keys.
Security vulnerabilities must not be reported publicly; see [SECURITY.md](SECURITY.md).

## Safety and liability

**Dosing errors can kill animals.** A wrong amount, a forgotten calibration, swapped tubes, a
software bug or a defect can put too much or too little of a substance into the tank within a
short time. Therefore:

- Keep containers no larger than needed: what is not in the container cannot reach the tank.
- Set a **daily limit** for every pump that safely rules out a dangerous amount.
- After every change (tube, container, schedule, firmware update) calibrate or check, and test
  the water more often in the following days.
- Route the tube ends so that nothing keeps flowing when the pump stands still (check valve or
  tube end above the water level).

The device runs on 24 V from an external power supply. Use only a certified power supply with
safety extra-low voltage (SELV), protect the electronics from splash and salt water, and plug
the power supply into a socket protected by a residual current device (RCD).

This project is provided without any warranty (see the licenses). Building and operating it is
at your own risk; the authors are not liable for damage to animals, aquarium, furnishings or
other property.

## License

- Firmware and software: **GPL-3.0-or-later**, see [LICENSE](LICENSE) and
  [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt).
- Hardware (enclosure, schematic, circuit board, Gerber): **CC BY-NC-SA 4.0**, see
  [LICENSE-hardware.md](LICENSE-hardware.md). Commercial use only with written permission.
- Name and logo of LG Aquatic Life: not covered by these licenses, see
  [TRADEMARKS.md](TRADEMARKS.md).

Copyright © 2026 Lars Greiss / LG Aquatic Life
