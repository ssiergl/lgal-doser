# Changelog

All notable changes to the LGAL-Doser firmware. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), versions follow
[Semantic Versioning](https://semver.org/). The current version is in `version.txt`.

## Unreleased – 0.1.0

First complete firmware (milestones M0–M6). Tested with host tests, fuzzers and the device
mock; the test on the finished hardware is still pending.

### Device and hardware

- Safe start: the drivers stay off until the firmware enables them; strapping pins are not
  driven during boot.
- TMC2209 over single-wire UART (two buses), configured through registers only, StealthChop,
  driver status and StallGuard monitored while running.
- Step clocks for all five pumps at the same time (RMT and MCPWM/PCNT) with trapezoidal ramp.
- DS3231 clock with NTP sync, automatic drift correction and a warning for a flat backup
  battery; EEPROM for the run state.
- Fitting: fewer than five pumps possible, with a factory population set at provisioning
  (`provision.py --pumps`) that a factory reset restores.
- Board selected at build time: pins and pump count per circuit board, first board `d5v1`
  (LGAL-Doser 5, board v1.0); the model is shown in the web UI and in Home Assistant.
- Firmware for the module variants N8R8, N16R8 and N32R8.

### Dosing

- Calibration per pump with a wizard and reminder, retract against dripping, tube priming with
  a dead-man switch.
- Container tracking with range in days, warning and alarm threshold per pump.
- Hard daily limit per pump, tube operating hours.

### Schedule

- One plan per pump: daily amount spread over doses or single times; daily, weekdays or every
  N days; daylight saving time, behaviour after a power cut.
- All plans checked on save (overlap, daily limit, lock times between pumps).
- Boost over days or doses.

### Network, access and security

- LAN with Wi-Fi as fallback, setup Wi-Fi with captive portal and setup wizard, mDNS name
  derived from the device name.
- Own admin password, sessions in RAM, API keys, throttling of password guessing, origin check
  for changing requests; engineering access for the manufacturer only.
- Device secret per device in the `fctry` partition; firmware images contain no secret.

### Web UI

- Embedded in the firmware (Preact + htm, no build step), English and German, dark and light
  theme, simple and pro view.
- Overview, pump detail, schedule editor with 24 h bar, history with charts and event list,
  settings, maintenance, engineering menu, coral reef in the background.
- Live values over WebSocket.

### Integration and maintenance

- MQTT with TLS (public or own CA), Home Assistant discovery, optional control, events
  replayed after a lost connection.
- History up to 60 days with CSV export.
- Backup of the schedule per pump and of the whole device (engineering).
- Updates by file, from a fixed update source (release manifest) or from a URL; rollback on a
  failed start; update entity for Home Assistant. A device only accepts firmware for its own
  board and module variant.
- License texts of the firmware and the third-party software on the device.
