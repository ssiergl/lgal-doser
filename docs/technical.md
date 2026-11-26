<!-- SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# LGAL Doser – technical documentation

*English · [Deutsch](de/technik.md)*

Firmware of the "LG Aquatic Life" dosing pump (first device: ESP32-S3-DEV-KIT-N16R8,
5× TMC2209, W5500): structure, building, first start-up, interfaces and behaviour in detail.
Project overview: [README](../README.md); fixed hardware (pin assignment, circuit):
[CLAUDE.md](../CLAUDE.md).

Framework: **ESP-IDF v6.1.1**, C++ (gnu++2b), FreeRTOS.

## Structure

```
components/lgal_core/   LG Aquatic Life core: network, HTTP, OTA, LED, button, settings, log
components/doser_hal/   pin assignment, safe start, TMC2209 UART, step clocks, pumps, RTC/EEPROM
components/doser_app/   device logic: hardware diagnostics, dosing engine, scheduler, engineering and their REST APIs
components/webui/       web UI (M5): www/ (Preact + htm, no build), pack.py embeds it gzip-compressed
brand/                  brand repository as Git submodule: the logo (not GPL, see TRADEMARKS.md) and web/, the shared design of the pages (GPL)
main/                   wiring of the components (app_main)
tools/                  provision.py, ap_password.py, ota_upload.py, acceptance.py, endurance.py, release.sh, make_manifest.py, channel_manifest.py, release_notes.py, third_party_notices.py, sbom.py, mock/ (device mock for the web UI)
tests/host/             host tests (TMC protocol, current, ramp, calendar, time zones, JSON, dosing and schedule logic, LED alarms, history, backup, MQTT/Home Assistant, update manifest),
                        the pump lock while a move starts (real threads) and simulation of dosing engine and scheduler (`tests/host/run.sh`)
tests/fuzz/             fuzzers for all parsers of external input (`tests/fuzz/run.sh`)
tests/i18n/, tools/, size/, lint/  translations, Python tools, image size, ESLint and cppcheck (see Checks)
tests/e2e/              end-to-end tests of the web UI in Chromium against the mock (Playwright, see Checks)
partitions*.csv         final partition tables per flash size (can only be changed over USB!)
boards/                 board definitions (model name per board; pins in components/doser_hal/include/doser/boards/)
variants/               configuration per module variant (N8R8, N16R8, N32R8)
.github/workflows/      CI (tests, fuzzers, all variants) and release build per git tag
docs/                   documentation, German versions in docs/de/
hardware/               enclosure and circuit board files per version, e.g. d5v1/ (CC BY-NC-SA 4.0)
```

## Toolchain

```bash
git clone -b v6.1.1 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v6.1.1
```

This repository with the brand submodule (`brand/`, the logo and the shared web design):

```bash
git clone --recursive https://github.com/ssiergl/lgal-doser.git
```

In an existing checkout: `git submodule update --init`; a newer logo or design with
`git submodule update --remote brand` and a commit.

```bash
~/esp/esp-idf-v6.1.1/install.sh esp32s3 && brew install cmake ninja
```

In every new shell:

```bash
source ~/esp/esp-idf-v6.1.1/export.sh
```

Build:

```bash
idf.py build
```

Host tests (without ESP-IDF, run on the Mac):

```bash
tests/host/run.sh
```

The same tests under AddressSanitizer and UndefinedBehaviorSanitizer (finds memory errors,
overflows and undefined behaviour):

```bash
SAN=1 tests/host/run.sh
```

Fuzzers for everything that reads external input: API bodies (JSON, backup, network, MQTT,
schedule, locks), query values, cookie/Authorization headers, WebSocket origin, update manifest
and DNS packets in the setup Wi-Fi. They mutate valid sample documents at random (byte and JSON
value level) and always run under both sanitizers. Arguments: rounds per target (default
200000) and seed; a crash can be reproduced with the same seed.

```bash
tests/fuzz/run.sh 1000000 2
```

After changes to `sdkconfig.defaults`, `sdkconfig` must be regenerated
(`rm sdkconfig && idf.py build`). The IRAM options of the step clocks are checked at build time.

Both test scripts take another compiler with `HOST_CXX=clang++` (the CI uses that).

#### Checks

The CI also runs these checks; locally from the repository root:

| Check | Command | Fails when |
|---|---|---|
| Translations | `python3 tests/i18n/check_translations.py` | a `lang/*.json` misses a key, has an unknown one or other placeholders (`{n}` …) than English; the code calls `t('…')` with a key English does not define |
| Python tools | `python3 -m pytest tests/tools` | manifest, beta channel, release text, passwords or provisioning values are wrong; the acceptance script (`tools/acceptance.py`) no longer finds against the mock what it should (locked API, a stored crash, a driver that does not answer, a changed setting, a test dose, an update to the other partition), or the long-term test (`tools/endurance.py`) matches the planned doses with the history wrongly or misses a restart or a driver that does not answer; passwords and version order are checked against the same values as the firmware's host tests |
| Mock contract | `python3 -m pytest tests/tools/test_api_contract.py` (part of the Python tools) | the web pages call an API path or method the firmware does not register; the mock (`tools/mock/`) does not answer a firmware route, knows a path the firmware does not have, or answers a field or sends an error code that occurs nowhere in the firmware sources; over the WebSocket the status frame and the replies to ping, priming, an unknown type and broken JSON have other fields or another shape than the firmware's (the end-to-end tests run against the mock only, so a mock that drifts from the firmware would let them pass while the device fails) |
| Image size | `python3 tests/size/check_image_size.py build-d5v1-N8R8` | the app image fills more than 85 % of its OTA slot (an image too large cannot be installed over the air, and the slot size cannot be changed by OTA) |
| ESLint | `npm ci --prefix tests/lint`, then `tests/lint/node_modules/.bin/eslint -c tests/lint/eslint.config.js components/webui/www` | the web UI uses an undefined name, has an unused variable or another error ESLint recommends against (no build step would find it) |
| cppcheck | `tests/lint/cppcheck.sh` (needs cppcheck) | the firmware sources have a warning, performance or portability finding; findings checked and found false are listed with their reason in `tests/lint/cppcheck-suppressions.txt` |
| End-to-end (web UI) | `npm ci --prefix tests/e2e`, once `npx --prefix tests/e2e playwright install chromium webkit firefox`, then `npx --prefix tests/e2e playwright test -c tests/e2e` | in Chromium, WebKit (Safari, iPhone) or Firefox one of these does not work: sign-in and an expired session (back to the same page after signing in), stay signed in and sign out everywhere, the wait after too many wrong passwords, overview, pump page (daily amount, dose and stopping it, hold to fill, tube replaced, a boost above the daily limit), pump settings (container refilled or changed or not tracked, name, operation off or manual only, fine-tuning with its hint in the simple view), what the simple and the pro view show, the complete calibration (measure, apply, implausible value, discard), schedules (single times, weekdays, every N days, catch-up, invalid amount, spread doses turned into single times, a plan above the daily limit refused, export and import into another pump), boost over days and over doses, lock time rules (invalid rules refused, a rule that collides with the schedules refused with the doses, a manual dose during a lock is refused with the time or waits), board v1.1 without or with too low 24 V supply (banner, every start refused, voltage on the maintenance page), dialogs (modal, focus, Escape), history (chart with the values of a day under the pointer, the text of every kind of event, event list with filters, paging and CSV export), settings (language, theme, time and time zone, status LED, admin password and API keys (copied over plain HTTP too), network with static addresses and Wi-Fi search, MQTT with an own CA and moving the topics), firmware updates (file, release, channel, update source, URL, a failed update, restart, factory reset), backup and restore, the engineering area locked and unlocked (values checked and saved, tubes, diagnosis, clock aging by hand, history retention, EEPROM test, danger zone, channel population with restart: the pump is gone everywhere), the setup wizard from the factory state (Wi-Fi search, a wrong and a right password, an own POSIX time zone and time server), the emergency page `/recovery` (device data, firmware upload, reboot), device name and host name, a lost live connection (reconnects by itself; priming stops), data right after loading, or the phone layout (no sideways scrolling, no clipped button in any of the 9 languages); a page logs an error; axe-core finds a WCAG 2.1 AA problem (contrast, missing names of fields and buttons) on a page (the engineering area locked and unlocked included), the sign-in, the setup or a dialog, in the light or the dark theme |

The end-to-end tests start their own mocks on ports 8771 to 8774 (8772 in the factory state,
`MOCK_FACTORY=1`; 8773 and 8774 as board v1.1 with 8 V and 18 V supply, `MOCK_BOARD_REV=1.1
MOCK_SUPPLY=…`), so a mock on 8767 keeps running. They take the test passwords from
`tools/mock/sys_mock.py`. The mock keeps one state for all browsers, so the tests run one after
another and each starts signed in with the device language English; what a test changes it puts back
(the `cleanup` fixture, also after a failure or a timeout). Hooks of the mock for the tests (POST):
`/mock/kill` drops the live connections, `/mock/firmware` (`?rolledback=1`: after a failed update)
and `/mock/time` put firmware and clock back. `-c tests/e2e --project desktop` runs
only Chromium (projects: `desktop`, `phone`, `safari`, `iphone`, `firefox`). After a failure `npx --prefix
tests/e2e playwright show-trace tests/e2e/results/<test>/trace.zip` replays the run step by step; in
the CI the report is attached to the run. Playwright's Firefox does not start on macOS 27 ("Could
not find profile folder"); there the `firefox` project runs in a Debian container
(`tests/e2e/firefox.Dockerfile`, build and run command in the file) with the repository mounted, or
in the CI.

### Boards

A board is one circuit board with its pin assignment and number of pumps. The firmware is built
for exactly one board; board and module variant (below) can be combined freely.

| Board | Model | Pumps | Circuit board |
|---|---|---|---|
| `d5v1` | LGAL Doser 5 | 5 | v1.0 (11/2026) |

Without an option `idf.py build` builds `d5v1`; another board: `idf.py -DBOARD=<id> build`. The
ID reads d = doser, number of pumps, v = circuit board version.

Revisions of a circuit board keep the board ID, the pins and the firmware image. The device
learns its revision from the factory partition (`provision.py --board-rev`, missing = 1.0,
`doser::hwconfig::board_rev()`); a newer revision only switches on inputs the older one does
not have. The serial banner, the start log, `/api/v1/hw/status` and `/api/v1/system/info`
(`board_rev`, in the web UI maintenance card, pro view) show it.

A board consists of two files:

- `boards/<id>.cmake` sets `LGAL_BOARD_MODEL` (model name in the web UI, system info and Home
  Assistant).
- `components/doser_hal/include/doser/boards/<id>.h` defines the pins in `doser::pins`: number of
  pumps (1–5), STEP pin, TMC2209 bus and address per pump, 1 or 2 UART buses, EN,
  Ethernet, I²C, LED and button. `doser/pins.h` lists the required names and checks them at
  compile time.

Every image carries its board ID (custom app descriptor, `lgal/ota_image.h`). A device takes only
updates for its own board: the update check picks the build of its board from the manifest, and
an uploaded image for another board is rejected before anything is written (`wrong_board`).

Records, backups, API and web UI are sized for five pumps (`doser::kMaxPumps`) on every board;
the pumps a board does not have are never fitted. The step clocks come from RMT for pumps 1–4
and MCPWM for pump 5. A board with fewer pumps uses only what it needs.

### Module variants

The firmware exists for three ESP32-S3 modules with the same pin assignment and 8 MB octal
PSRAM. They differ only in flash size and thus in the partition table:

| Variant | Flash | Modules (example) | Partition table | App slots | Storage (history, files) |
|---|---|---|---|---|---|
| `N8R8` | 8 MB quad | ESP32-S3-WROOM-1-N8R8 | `partitions_8mb.csv` | 2 × 3 MB | 1.75 MB |
| `N16R8` | 16 MB quad | ESP32-S3-WROOM-1-N16R8 (Waveshare DEV-KIT-N16R8) | `partitions.csv` | 2 × 5 MB | 5.75 MB |
| `N32R8` | 32 MB octal, 1.8 V | ESP32-S3-WROOM-2-N32R8V | `partitions_32mb.csv` | 2 × 5 MB | 21.75 MB |

Without an option `idf.py build` builds the N16R8 variant. Another variant builds into its own
directory with its own `sdkconfig`:

```bash
idf.py -B build-N8R8 -DVARIANT=N8R8 build
```

A device recognises its variant by the flash size of its image and takes the matching file from
the manifest when updating. It rejects an image for a different flash size on upload
(`wrong_variant`). N32R8 is only built, not tested on hardware (ESP-IDF supports flash above
16 MB only as octal flash, hence the WROOM-2 module).

### Memory

| Memory | Holds |
|---|---|
| Internal RAM (~340 KB usable, ~120 KB free in operation) | code that must run during flash writes (step clocks), task stacks, network stack (lwIP, Wi-Fi, W5500 buffers), HTTP and WebSocket connections, sign-in sessions |
| PSRAM (8 MB) | log buffer (32 KB, engineering menu), every allocation above 16 KB, all TLS connections (update check, OTA download, MQTT over TLS) |
| Flash `nvs` | settings, plans, calibration, network and MQTT access, admin password hash and API keys |
| Flash `fctry` | device secret, board revision, fitting (kept by a factory reset) |
| Flash `coredump`, `ota_0`/`ota_1` | the last crash; the two firmware slots with the web UI inside |
| Flash `storage` (LittleFS) | history |
| EEPROM AT24C32 (RTC module) | run state per pump (two slots with CRC); without EEPROM it goes to NVS |

TLS allocates in PSRAM (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`): the update check alone took up to
~75 KB of internal RAM, a lasting MQTT TLS connection about 35 KB more. The network stack stays in
internal RAM: it is faster there, and the W5500 driver hands its buffers to SPI DMA.

### Release

`tools/release.sh` builds every board (`boards/`) in every variant into `dist/`: per board and
variant the app image `lgal_doser-<version>-<B>-<V>.bin` (OTA, e.g. `lgal_doser-0.1.0-d5v1-N16R8.bin`) and
`lgal_doser-<version>-<B>-<V>-full.bin` (bootloader, partition table, OTA data and app for the first USB
flash at address 0x0; an update upload rejects it with `full_image`, since it starts with the
bootloader) and the SBOM `lgal_doser-<version>-<B>-<V>.spdx.json` (see below), plus
`manifest.json` (`tools/make_manifest.py`), `LICENSE` and `THIRD-PARTY-NOTICES.txt`. `BOARDS="d5v1" VARIANTS="N16R8" tools/release.sh` builds only some.
The CI builds the same combinations (matrix in `.github/workflows/ci.yml`: add a new board
there).

On GitHub `.github/workflows/release.yml` does this: raise the version in `version.txt`, commit,
push the tag `v<version>`. The workflow checks that tag and `version.txt` match, builds
everything and at the same time runs the whole CI (`ci.yml`) on the tagged commit. Only when both
passed does it publish the release; a failed test leaves neither a release nor a changed beta
channel behind (a release takes as long as the slowest CI job, about 20 minutes). The images contain no secret (see
[First start-up](#first-start-up-once-over-usb-before-installation)), so the workflow needs no
repository secrets. Tags with a pre-release part (`v1.0.0-rc.1`) become pre-releases, which
`releases/latest` skips. The devices then use
`https://github.com/<owner>/<repo>/releases/latest/download/manifest.json` as update source.

The text of the release page comes from `tools/release_notes.py`: the section of `CHANGELOG.md`
whose heading names the version (a release candidate `0.2.0-rc.1` uses the section of `0.2.0`,
e.g. `## Unreleased – 0.2.0`) as a summary in bullet points, then every commit since the
previous version tag, folded away under "All changes", and a link to the full comparison. So
keep the changelog section up to date before tagging.

#### SBOM

Every release carries a software bill of materials per board and variant,
`lgal_doser-<version>-<B>-<V>.spdx.json` (SPDX 2.2 JSON): every package linked into the image with
version, supplier, license and, where known, CPE and package URL – ESP-IDF and its parts, the
managed components, Preact and htm, and the own components. `tools/sbom.py` writes it with
Espressif's [esp-idf-sbom](https://github.com/espressif/esp-idf-sbom) from the build's
`project_description.json`:

```bash
pip install esp-idf-sbom==1.5.0
python tools/sbom.py build-d5v1-N16R8 lgal_doser-0.1.0-d5v1-N16R8.spdx.json
```

The `sbom.yml` files describe what esp-idf-sbom cannot know: the project root and every own
component (supplier, GPL-3.0-or-later), `components/webui/sbom_preact.yml` and `sbom_htm.yml`
the vendored libraries. After an update of Preact or htm raise the version there too. The CI
creates the SBOM of every build, so an invalid `sbom.yml` fails before a release.

Known vulnerabilities of the packages (NVD, by CPE) are listed by:

```bash
esp-idf-sbom check lgal_doser-0.1.0-d5v1-N16R8.spdx.json
```

For the [beta channel](#ota-update) every release also updates the pre-release `channel-beta`
(created on the first release): it holds only a `manifest.json` that names the newest version,
release candidate or release, with absolute addresses into the release of that version
(`tools/channel_manifest.py`). An older release, e.g. a hotfix while a newer release candidate
is out, leaves it as it is. Devices on the beta channel read
`https://github.com/<owner>/<repo>/releases/download/channel-beta/manifest.json`.

`.github/workflows/ci.yml` runs the host tests under both sanitizers, a short fuzz run, the
checks, the end-to-end tests of the web UI and the build of all three variants (in the
`espressif/idf:v6.1.1` container) on every push to `main`, every pull request and every release.

## First start-up (once over USB, before installation)

Bootloader, rollback setting and partition table are written **only** by the USB flash. After
that all updates run over OTA.

Every device has its own secret in the `fctry` partition. From it the firmware derives the
device password (rating plate, setup Wi-Fi, first sign-in, password recovery) and the
engineering key. The firmware image itself contains no secret, so releases can be public.
Factory reset and OTA updates leave `fctry` untouched.

1. **Create the manufacturer master** (once, for all devices): a file `factory.secret` in the
   project folder with a long random line (at least 16 characters), e.g.
   `python3 -c "import secrets; print(secrets.token_hex(32))" > factory.secret`. It is not in
   git and never gets into the firmware. **Keep it safe and back it up:** without it the
   rating plate password and engineering key can no longer be computed from the MAC; whoever
   has it can do so for every device.

2. **Flash over USB** (board not yet plugged in), variant matching the module (see
   [Module variants](#module-variants)):

   ```bash
   idf.py -p /dev/cu.usbmodem* flash
   ```

   or `idf.py -B build-N8R8 -DVARIANT=N8R8 -p /dev/cu.usbmodem* flash`. From a release the
   complete file works too:
   `python -m esptool --chip esp32s3 write-flash 0x0 lgal_doser-0.1.0-d5v1-N16R8-full.bin`
   (it also overwrites `fctry`, so always do step 3 afterwards).

3. **Provision:** reads the MAC, writes the device secret
   HMAC-SHA256(master, "LGAL-DEVICE" + MAC) to `fctry` and shows the values for the rating
   plate:

   ```bash
   python3 tools/provision.py -p /dev/cu.usbmodem*
   ```

   With fewer pumps built in than the board has, `--pumps 1,2,3` also stores the factory
   population: the pumps fitted on delivery and again after every factory reset (default: all
   pumps of the board). `--board-rev 1.1` stores the revision of the circuit board (default
   1.0, see [Boards](#boards)). Provisioning again (e.g. after a full flash) needs the same
   values.

   **Device password on the rating plate** (e.g. a sticker inside the enclosure), not the
   engineering key. Both can be recomputed from the MAC at any time:

   ```bash
   python3 tools/ap_password.py AA:BB:CC:DD:EE:FF --engineering
   ```

   Afterwards `idf.py -p /dev/cu.usbmodem* monitor` shows MAC, host name, setup Wi-Fi and
   password at start.

   **Without provisioning** (e.g. a build from a release) the device creates a random secret
   itself at first start and shows device password and engineering key once on the serial
   console. Write these values down at once: nobody can compute them again. Provisioning later
   replaces them.

4. Plug in the board, connect the LAN cable.

## Access

| Way | Address | Sign-in |
|---|---|---|
| LAN | `http://lgal-doser-xxxx.local/` or DHCP/static IP | `/login` |
| Wi-Fi (fallback or fixed, see [Network](#network-m4)) | `http://lgal-doser-xxxx.local/` or IP | `/login` |
| Setup Wi-Fi `LG-Aquatic-Life-Setup-XXXX` | `http://192.168.4.1/` | Wi-Fi: device password, web: `/login` |

Sign-in on `/login`, a pure sign-in page with the logo, the user "admin" (shown for password
managers, the device has only this one user) and the password. Signed-in browsers are sent on to
the target right away; password, API keys and engineering live in the web UI.
- **Device password** (rating plate): it never signs in. It only sets the first own admin
  password (8–64 characters) in step 2 of the [initial setup](#initial-setup-m4) and resets a
  forgotten one through the setup Wi-Fi. Without an admin password `/login` leads to the setup.
- **Sessions** (cookie `lgal_session`, max. 16, only the SHA-256 of the token is kept) live in
  RAM: 7 days without use or 30 days absolute, sign in again after a restart or OTA. With
  **Stay signed in** on the sign-in page the cookie outlives the browser and the session is
  stored in NVS (`auth`/`sess`, max. 8; the last use is written at most once a day): it lasts
  90 days after its last use, across restarts and updates. Normal sign-ins never push out a
  stored one. Changing the password signs out all other browsers; **Sign out everywhere**
  (settings → sign-in) ends every session, stored ones included. API keys stay valid.
- **API keys** (max. 4) for apps, Home Assistant and scripts, header `X-API-Key: lgal_…` or
  `Authorization: Bearer lgal_…`. The key is shown only once; only its SHA-256 is stored. Keys
  survive restarts and password changes.
- **Failed attempts:** 5 free, then a wait per IP from 1 s, doubling up to 300 s. An IP is only
  forgotten after 1 h of quiet; if all 8 slots are taken, further IPs share one common wait
  (many addresses slow each other down).
- **Forgotten password:** hold the button for 3 s → setup Wi-Fi. Connected through it, `/login`
  can set a new admin password with the device password. Settings, schedules and calibration
  are kept.
- **Engineering** (super admin, manufacturer only): tap the firmware version in the web UI footer
  7 times, key per device derived
  from the device secret (`tools/ap_password.py --engineering`). It applies to the signed-in
  session and locks 15 min after the last engineering request. With an API key, additionally
  the header `X-Engineering-Key`. On start the web UI locks an unlock that does not come from
  the same browser tab – a closed tab therefore stays locked.

Browser pages without sign-in redirect to `/login?next=…`; API calls answer 401
`unauthorized` or 403 `engineering_locked`.

The device accepts changing requests (everything except GET) from a browser only from its own
pages (Origin = Host, otherwise 403 `forbidden_origin`), including sign-in, sign-out, password
change, API keys and engineering unlock. In addition to the `SameSite=Lax` cookie (sent from
another site only when a page is opened, so a link or the typed address works; `Strict` made
Safari leave it out then and the page fell back to the sign-in page), this protects against
other devices under the same home domain. Clients without an Origin header
(Home Assistant, apps, scripts) are not affected.

In factory state (`setup_completed = false`) the setup Wi-Fi is always on and `/` leads to
the [initial setup](#initial-setup-m4). Until it is completed a signed-in browser reaches only the
setup itself: the wizard, sign-in, network, time, language, system information, the network and
maintenance pages and firmware updates. Everything else – pumps, schedules, history, backup,
MQTT, engineering, the web UI and its WebSocket, new API keys – answers 409 `setup_required`
(pages: redirect to `/setup`). So nobody gets to the pumps before an own password is set and the
setup is finished; for a test run before shipping, set the device up and reset it afterwards.

## Initial setup (M4)

1. Connect to the setup Wi-Fi `LG-Aquatic-Life-Setup-XXXX` (password from the rating plate).
   The **captive portal** usually opens the setup on phones and laptops by itself; otherwise
   open `http://192.168.4.1/`.
2. The page **`/setup`** guides through five steps. The first two need no sign-in: language →
   own admin password, set with the device password from the rating plate (`auth/claim`, also
   stores the language). Then the browser goes to `/login` and signs in once with the new
   password (user "admin" – the password manager can save it here); after that the wizard goes on
   with network (LAN status, scan Wi-Fi, test, save) → time zone, NTP server, optionally time from
   the browser → finish. An interrupted setup goes on after signing in at the network step.
3. **Finishing** is only possible with an admin password and a connected LAN or Wi-Fi. The page
   shows the new addresses (`http://lgal-doser-xxxx.local/` and IP); 20 s later the setup Wi-Fi
   switches off.

| Topic | Behaviour |
|---|---|
| Captive portal | DNS server on the setup Wi-Fi answers every name (A) with `192.168.4.1`, other types empty. Unknown pages redirect setup Wi-Fi clients to `http://192.168.4.1/`. LAN/Wi-Fi clients get a normal 404. |
| Repeat | `/setup` stays reachable after finishing (link on the maintenance page), but then does not switch the setup Wi-Fi on again. |
| Factory reset | Button 10 s, maintenance page (enter the admin password again) or engineering menu erases everything (see [Button and LED](#button-and-led)) → initial setup starts over. |

## Network (M4)

In the web UI under **Settings → Network** (status for everyone, settings, Wi-Fi scan and connection test in the pro view).

| Topic | Behaviour |
|---|---|
| Interface | `auto` (default): LAN has priority. If the LAN has no address for 30 s (cable out, no DHCP), the Wi-Fi client starts; as soon as the LAN has an address again, the Wi-Fi radio goes off. `lan`: LAN only, Wi-Fi stays off. `wlan`: Wi-Fi only, Ethernet is switched off. |
| Addresses | LAN and Wi-Fi separately: DHCP or static (IP, mask /8–/30, gateway, DNS 1/2). The DNS servers follow the active interface. |
| Wi-Fi | WPA2 or newer (password 8–63 characters) or open. Reconnect with a wait of 2 s, doubling up to 60 s. No power saving (the device runs on mains): the radio stays awake, so answers do not wait up to a beacon interval (~100 ms). |
| Applying | Saved at once (NVS `net`), applied 1 s after the response – otherwise the response would not reach the browser at a new IP. |
| Connection test | Checks SSID/password/addresses without saving (max. 25 s); afterwards the saved settings apply again. |
| Locked out? | Button 3 s → setup Wi-Fi, open `http://192.168.4.1/` there, sign in and change it under Settings → Network (pro view). The setup Wi-Fi runs in parallel to the Wi-Fi client. |

Settings (`PUT /api/v1/net/settings`; missing fields stay, empty address = none):

```json
{"mode": "auto",
 "lan":  {"dhcp": false, "ip": "192.168.1.50", "netmask": "255.255.255.0",
          "gateway": "192.168.1.1", "dns1": "192.168.1.1", "dns2": ""},
 "wlan": {"ssid": "Reef", "password": "…", "dhcp": true}}
```

The Wi-Fi password is never returned (`"password_set": true`); without `password` the stored one
stays. Errors: 400 `invalid_param` with `field` (e.g. `lan.gateway`), 400 `wlan_not_configured`
for `wlan` without SSID.

## WebSocket (M4)

**`/api/v1/ws`** for live values and priming. Sign-in as for the REST API (session cookie or
API key header). Messages are JSON text frames.

| Topic | Behaviour |
|---|---|
| Status push | `{"type":"status","seq","utc","time_valid","pumps":[…]}` right after connecting, on every change (checked every 250 ms) and at least every 10 s. Per pump only the live values: activity, phase, progress (`done_ml`, `target_ml`, `rpm`, `prime_s`), container, daily amount, last run. Everything else comes from `GET /api/v1/pumps`. A client that stops taking data (phone locked, VPN gone) is disconnected as soon as a push waits 2 s for room in its send buffer; the pushes run in the web server, so a longer wait would hold up every request. The connection is reset (`SO_LINGER` 0) rather than closed gracefully, so its unsent frames do not hold RAM while the device would retransmit them to a sleeping client for minutes. |
| Requests | `{"type":"…","id"?:n,…}` → answer with the same `type`/`id`, `"ok"` and on error `"error"` (code). `{"type":"ping"}` for testing. When its sockets (20) run out, the device closes the connection with the oldest traffic; a delivered status push counts, so live connections stay open and idle browser connections go first. The web UI still pings about every 30 s. |
| Priming | `{"type":"prime","pump":1,"action":"start\|beat\|stop","tier"?:0..2}`. While holding, `beat` every 150 ms. If the connection drops, the pump stops at once (additionally after `prime_heartbeat_ms` without heartbeat). |
| Security | Browsers only from the device's own pages (Origin = Host, otherwise 403 `forbidden_origin`). Max. 4 connections (503 `too_many_clients`). Sign-out, password change or a deleted API key end the connection (checked every 5 s and on every message). |

The web UI uses the WebSocket for live values and priming and falls back to polling and REST
priming without it.

## Web UI (M5)

`http://lgal-doser-xxxx.local/` opens the UI (sign-in as under [Access](#access)). It is
embedded in the firmware: `pack.py` gzip-compresses `components/webui/www/` and, from `brand/`, the logo and the shared design at build time
(≈ 104 KB), and it is updated with the firmware over OTA – UI and API thus always match, and the
rollback covers the UI as well. Preact and htm sit unchanged in `www/vendor/` (versions and
checksums in the README there); there is no Node build, `idf.py build` is enough.

The look every LG Aquatic Life device shares comes from the brand repository (`brand/web/`, GPL):
`lgal.css` (colour tokens of both themes, cards, buttons, forms, switches, dialogs, top bar,
background; served as `/ui/lgal.css`), `reef.js` (background reef, `/ui/lib/reef.js`) and
`flags.js` (flags and language picker, `/flags.js`). `www/style.css` holds only what is the
doser's own (pump colours, dashboard, pump pages, …) and loads after `lgal.css`; the sign-in and
setup pages need `lgal.css` only. Shared styles are changed in the brand repository, never
overridden here.

| Topic | Behaviour |
|---|---|
| Pages | Overview, pump (detail with schedule), history, settings, maintenance (`/#/maintenance`, from the settings). Addresses as hash routes, e.g. `/#/pump/3` or `/#/history/3` (events pre-filtered to pump 3); old links `/#/schedule/3` open the pump. |
| Overview | Two layouts: from 940 px width (desktop, tablet landscape) two columns, the fifth card equally wide at the bottom left; below that one column (mobile, tabs as a bar at the bottom), on very narrow phones long values wrap. Per pump: container graphic with level (border yellow if it lasts fewer days than the warning threshold, default 7, red below the alarm threshold, default 4; both thresholds configurable per pump, dashed without container tracking), dosed / planned today (turquoise with boost, incl. the boost share of today's doses), range in days (from the schedule), next dose (highlighted with boost), running dose. Next to the status only small icon badges with counts for errors and warnings plus a boost icon (text as tooltip), so that all cards stay the same height; the texts (driver fault in the last run, container almost empty, calibration missing or too old, daily limit, tube, lock time, skipped doses) are in the pump view. |
| Pump | Container card as summary, the same size for all pumps, below it one card each for alarms (red) and warnings (yellow) with the texts of the overview badges, only if there are any: large container graphic, dosed / planned today, range, next dose (with boost share; below it "incl. boost (+x ml per dose)" in turquoise), plus "Refilled" and "Change" (size 0 = no tracking); dose now with progress and stop (lock time: message with end or "starts afterwards"), calibration wizard (start dialog asks whether the tube is primed and primes it there by holding – WebSocket with dead-man, otherwise REST; the only place to prime, so that nothing runs by accident → measuring cup → apply or discard the measured amount), usage (daily limit, last run, total, tube hours; "Tube replaced" only in pro – afterwards "New tube – calibrate again" shows as a warning at the pump until it is calibrated) and settings. Name, mode (automatic / manual only / off) and calibration reminder (off, 3, 6, 9 or 12 months, default 3) for everyone; speed when dosing, target amount of the calibration and fine-tuning only in pro. Simple shows active pro values as a hint. |
| Schedule | Card in the pump detail: today's doses on a 24 h bar with the current time, below it the plan – exactly one per pump. **Simple:** daily amount in N doses between two times, daily, with a preview of the times; a plan created in pro is only summarised and stays active (edit it in pro or replace it with a simple plan). **Pro:** plan kind "spread" (daily amount in N doses) or "single times" (up to 24 times with their own amount), daily / weekdays / every N days, and the behaviour after a power cut. Whether the plan runs is decided by the pump's mode (automatic / manual only). The bar shows unsaved changes at once; grid, daily limit and lock time conflicts are reported with the time or pumps and times. In pro: export the pump's schedule as a JSON file and import it (also on another pump); the device checks the imported plan like an edited one. |
| History | For everyone: dosed amount per day as a small chart per pump (7, 14 or 30 days, or any period with two date fields, back as far as the history is kept), as bars (default) or line (remembered in the browser), with labelled axes (ml; weekdays or date, "today" always labelled); pointing or tapping names day, amount, number of doses and skipped doses (dot below the axis), plus a table view. Pro only: event list (doses, skipped doses, priming, calibration, container, tube, alarms) with filter by pump, kind and period (the same choice), "Load more" and CSV export. The pump detail links to the pump's history under "Usage". |
| Boost | Card in the pump detail (simple and pro): extra amount over 1–30 days with the preview "≈ X ml extra per day", in pro optionally over the next 1–100 scheduled doses (preview "≈ X ml extra per dose", ends with the last of them), hint when there is no schedule or the daily limit would be exceeded. If the mode is not automatic, the card only says so in one line ("The pump is off – no boost possible."), a running boost is then paused but can still be ended. A running boost shows the rest, progress, end (day and time) and the remaining doses with their share, and can be ended; overview and header show it as well. |
| Lock times | Pro only, in the settings: pump pairs with a minimum gap in minutes (up to 10 rules) and whether a manual dose during the lock time is rejected or waits. On save the device checks all schedules against the rules and reports collisions. |
| Time | In the settings: device time and source (time server, clock module, by hand), warning for more than 2 min difference to the browser, a silent clock module or a lost time (backup battery), "Take the time from this browser". Time zone (list as in the setup wizard or POSIX string, suggestion from the browser) for everyone; time server (NTP) and last sync only in pro; date and time by hand (pro) only without time server – with a time server the device sets the clock itself. |
| Status LED | In the settings for everyone: what the LED shows right now, the meaning of the colours and the night mode (time window, ready light at night off / 10 / 25 / 50 %). |
| Network | In the settings: status (active interface, LAN and Wi-Fi address, signal, setup Wi-Fi, countdown to the Wi-Fi fallback) for everyone. Pro only: interface (automatic / LAN only / Wi-Fi only), DHCP or static addresses per interface (prefilled with the current ones), Wi-Fi scan, password, connection test and save; if the static LAN address changes, the page names the new one. |
| MQTT | In the settings: connection status with the last error (host name, unreachable, TLS, sign-in) for everyone. Pro only: broker and port, encryption (none / TLS with public certificate / TLS with own CA certificate incl. upload, replace, remove), user and password, base topic, Home Assistant discovery with prefix, "Allow control via MQTT" with a warning and the list of topics. See [MQTT / Home Assistant](#mqtt--home-assistant-m6). |
| Sign-in | In the settings: change the password (or set it for the first time) and sign out for everyone; API keys (list, create with one-time display and copy, delete) only in pro. `/login` stays for sign-in and the forgotten password. |
| Maintenance | Firmware version, build date, address, uptime and a hint after a rollback for everyone; only with an [update source](#ota-update) the card "Updates": installed and available version (badge "Up to date" / "New: x.y.z"), time of the last check, link "What's new", "Check for updates", "Install x.y.z" with confirmation and progress, switch "Check daily", pro only: update channel stable/beta (beta with a warning, then a "Beta" badge and notice in every view); firmware update by `.bin` upload with progress (locked while a pump runs), afterwards the page waits for the restart and reloads; restart with confirmation; factory reset with the admin password entered again; licenses (GPLv3 and third-party software). Pro only: warning while http is allowed for updates, storage and OTA state, MAC, last reset reason, memory, log and crash report (download, delete). |
| View | **Simple** / **Pro**, stored per browser. Only in pro: detailed schedules, pump fine-tuning (speed, retract, daily limit), rules between pumps (lock times, queueing, power cut) plus network and MQTT settings, time server and API keys. What is set in pro stays active in simple. |
| Live values | Over WebSocket; without a connection the page polls every second. |
| Device | In the settings: device name (up to 32 bytes, e.g. "Reef tank") for header, Home Assistant and mDNS service. The host name follows from it (lower case, umlauts as ae/oe/ue/ss, otherwise "-", e.g. `reef-tank.local`); pro only: own host name (a–z, 0–9, "-", up to 32 characters). Without a name it stays `lgal-doser-xxxx`. mDNS takes the name at once, the router at the next address assignment. MQTT topics and Home Assistant IDs stay until "Move topics to the device name" is pressed in the MQTT card. |
| Appearance | Dark, light or as the system; language as on all device pages; time format 24 hours (default) or 12 hours with AM/PM for all shown times (remembered in the browser; time input fields follow the browser). In the background a coral reef in three depths, drawn by `reef.js` from the brand repository at load from a fixed seed (the same picture every time, ≈ 3 KB instead of ≈ 130 KB of SVG); seaweed sways, bubbles rise – off with "reduce motion". |
| Service | Click the firmware version in the footer 7× quickly → dialog for the engineering key, then straight to the [engineering menu](#engineering-menu-m6) (at once if it is already unlocked). The [backup](#backup-m6) and "Lock now" are there too. |
| Emergency | `/recovery` stays as a plain maintenance page without the web UI scripts (device info, firmware upload, restart, log, links to the web UI and setup; the shared design without logo, reef or animation) in case the UI does not load. It is deliberately not linked anywhere: type the address by hand, e.g. `http://lgal-doser-xxxx.local/recovery` (the user guide mentions it). |
| Files | `/ui/…` without sign-in (program code only, no device data), ETag and `no-cache`. |

### Mock server (without a device)

`tools/mock/` simulates the device for the web UI – Python standard library only, `localhost`
only:

```bash
python3 tools/mock/webui_server.py
```

Then open `http://localhost:8767/` (optional: project folder and port as arguments). The pages
come straight from `components/webui/www/`; after a change a reload is enough. REST API and
WebSocket run with state in memory: pumps with dosing, priming (dead-man) and calibration,
schedules, history, network, sign-in, MQTT, backup and engineering menu, plus setup wizard
(`/setup`), sign-in page and emergency page (`/recovery`). Test values: admin password
`test-admin-pw`, engineering key `test-eng-key`. It starts signed in; after signing out it
locks like the device: pages redirect to the sign-in, API calls answer 401; after five wrong
passwords the sign-in waits 1, 2, 4 … s (429). An address it does not know answers 404 with
the header `X-Mock-No-Route`, so the contract test (see Checks) finds firmware routes the mock
lacks. The state is lost on exit. Environment: `MOCK_FACTORY=1` starts in factory state; `MOCK_BOARD_REV=1.1` simulates
board v1.1 with 24 V sense, `MOCK_SUPPLY=<volts>` its supply (default 24.1; below 20 V doses
answer `supply_low`, below 12 V `supply_missing`). `MOCK_STRICT_AUTH=1` signs in per client like
the firmware (session cookie or API key, also `X-Engineering-Key`) instead of one shared state and
starts signed out; the tests of the acceptance script use it.

## OTA update

In the web UI under maintenance, via the emergency page (`/recovery`) by file upload, or by
script:

```bash
python3 tools/ota_upload.py lgal-doser-xxxx.local --key lgal_...
```

Sign-in: `--key` (or `LGAL_API_KEY`) or `--password` (or `LGAL_PASSWORD`, admin password). A new
device needs its admin password from the setup first (steps 1 and 2); updates work from then on,
also before the rest of the setup is done. With a password the script signs in again by itself
after the restart.

Procedure and protection:
- The image is checked before writing (ESP32-S3, project name `lgal_doser`, flash size, board) and
  verified by hash after writing.
- The new image starts as `PENDING_VERIFY` and is marked valid after 60 s of stable running as
  soon as the web server is reachable (LAN, Wi-Fi or setup Wi-Fi). If it is not yet (router
  restarting, Wi-Fi still connecting), the device checks again every 10 s for up to 5 min.
- Crash or watchdog before that, or still not reachable after these 5 min: automatic
  **rollback** to the previous firmware.
- From the moment an update is accepted no dose starts: a manual one gets 409 `locked`, a
  scheduled one waits (at most `max_delay_min`). The pump drivers are switched off for the
  duration of the update.

**Update source:** a fixed address of the release manifest, e.g.
`https://github.com/<owner>/<repo>/releases/latest/download/manifest.json`. Factory value from
`CONFIG_LGAL_OTA_SOURCE` in `sdkconfig.defaults`: the releases of this repository (a build from a
fork sets its own address there, empty = no source); it can only be changed in the
[engineering menu](#engineering-menu-m6). "latest" skips pre-releases. The manifest names the version and, per board
(`d5v1`, …) and module variant (`N8R8`, `N16R8`, `N32R8`, by flash size of the build), file,
SHA-256 and optionally size:

```json
{"version":"0.2.0","notes":"https://github.com/<owner>/<repo>/releases/tag/v0.2.0",
 "builds":[{"board":"d5v1","variant":"N16R8","url":"lgal_doser-0.1.0-d5v1-N16R8.bin","sha256":"<64 hex characters>","size":1234567}]}
```

- `url` absolute or relative to the configured manifest address (not to the target of a
  redirect); `notes` is only taken as an http(s) address. Manifest at most 4 KB.
- Versions follow Semantic Versioning; a pre-release (`1.0.0-rc.1`) is older than `1.0.0`. Only
  a version newer than the running one is offered.
- **Automatic check** (on by default): about 10 min after start, then daily, hourly after an
  error. It only reports – nothing is ever installed without an order.
- Installing loads the manifest once more, then the file of the device's own board and variant; if SHA-256
  or size do not match, the image is discarded before it becomes active. Otherwise as with an
  upload (locked while a pump runs; rollback protection).
- Source and file follow the https and http rules of the update from a URL.
- **Channel:** while the source is the factory one, the admin chooses in the pro view under
  Maintenance → Updates between **Stable** (factory setting) and **Beta**: the manifest of the
  beta channel (`CONFIG_LGAL_OTA_SOURCE_BETA`, see [Release](#release)) also offers release
  candidates. Switching to beta needs a confirmation with a warning; while beta is on, the card
  shows a "Beta" badge and a warning in every view. Back to stable never installs an older
  version: the device keeps the release candidate until a newer release is out (or the
  release file is uploaded). An own source from the engineering menu has no channel; setting
  one switches the channel back to stable.

**Update from a URL:** the device downloads the `.bin` itself, e.g. from a release page – in the
[engineering menu](#engineering-menu-m6) or over REST with engineering unlock:

```bash
curl -H "Authorization: Bearer lgal_..." -H "X-Engineering-Key: ..." -d '{"url":"https://example.com/lgal_doser.bin"}' \
     http://lgal-doser-xxxx.local/api/v1/ota/url
```

- By default only **https**; the server certificate is checked against the list of public
  certificate authorities included in the firmware. Up to 5 redirects.
- **http** can be allowed in the engineering menu (with confirmation). While that is active, the
  maintenance page (pro) shows a warning, and every http address is confirmed once more before
  the start: over http the firmware can be swapped unnoticed on the way. Without this permission
  a redirect from https to http is rejected as well.
- Progress and result also go to `<base>/ota/state` (MQTT).
- Rejected while a pump runs (`pump_running`) or an update is already running; the download runs
  in the background, check, restart and rollback as with the upload.

## Acceptance test on the device

`tools/acceptance.py` checks a device on the network, after every release candidate and at the
hardware test. It only reads unless asked to dose or update:

```bash
LGAL_API_KEY=lgal_... python3 tools/acceptance.py lgal-doser-xxxx.local --snapshot before.json
LGAL_API_KEY=lgal_... python3 tools/acceptance.py lgal-doser-xxxx.local \
    --ota build-d5v1-N16R8/lgal_doser.bin --expect-version 0.1.0-rc.9
python3 tools/acceptance.py lgal-doser-xxxx.local --dose 1:2      # asks for the password, then before dosing
```

- **Access:** every GET route of the firmware sources answers 401 without a sign-in (only
  `auth/status` and `system/language` are open), the WebSocket as well; a wrong password is
  refused; a change or a WebSocket from a foreign page gets 403; the engineering area is locked.
- **Firmware:** version (`--expect-version`), update confirmed (`VALID`; `PENDING_VERIFY` is a
  warning), no rollback, last start not a crash (panic, watchdog, brownout), no stored core dump,
  free memory.
- **Device:** clock and clock module, network, 24 V supply (board v1.1), no update running;
  per pump mode, last result, calibration and skipped doses (warnings).
- **Hardware** (with `LGAL_ENG_KEY`): every fitted driver answers on UART and has its registers
  set, no fault, drivers off at rest, error rate of both UART buses, clock module, EEPROM (if it is missing: the I²C bus scan of the
  last start from the log), history storage.
- **Live connection:** the WebSocket sends the status at once and answers a ping.
- **Settings:** `--snapshot` writes them (pumps, schedules, locks, network, MQTT, LED, time,
  updates, API key names – only with the password, the device lists its keys to a session only; with `LGAL_ENG_KEY` also the engineering values and drivers) to a file,
  `--compare` lists every difference. `--ota` uploads the image, waits for the restart, checks the
  other partition and runs all checks again, then compares the settings with the state before.
- **Test dose** (`--dose PUMP:ML`, up to 50 ml, repeatable): asks for a measuring cup first
  (`--yes` without asking); the result and the dosed amount are checked, the amount in the cup is
  for you to read.

Sign-in from the environment only (`LGAL_API_KEY` or `LGAL_PASSWORD`, else it asks for the
password), so no secret lands in the shell history. Exit code 0: passed (warnings allowed), 1: a
check failed, 2: not reachable or sign-in refused. A session's engineering access is locked again
at the end.

## Long-term test on the device

`tools/endurance.py` watches a device for 24 hours (`--hours`) without changing anything: the
pumps dose by their own schedules, so their tubes must end in a container. Every minute
(`--interval`) it reads the device, every 5 minutes the plans of the day and the hardware, and
every 15 minutes (`--report`) it prints a status line; restarts, outages, network changes and
driver problems show at once. `--log run.csv` keeps every sample (memory, response time,
network, supply, WebSocket frames, chip temperature). Ctrl-C ends the run early and checks what
was recorded. Keep the computer awake for the whole run, e.g. on a Mac:

```bash
LGAL_API_KEY=lgal_... LGAL_ENG_KEY=... caffeinate -i python3 tools/endurance.py lgal-doser-xxxx.local --log run.csv
```

At the end it checks:

- **Run:** every sample answered (an outage of 3 minutes or more fails), response times, no
  restart (uptime went back: fails, with the reset reason), no new core dump, the firmware did not
  change.
- **Memory:** lowest free memory, a leak (free memory in the last hour more than 16 KiB below the
  first hour, from 3 hours on), largest free block.
- **Clock and network:** valid time all along, at most 10 s from this computer's clock, an NTP
  sync (from 4 hours on, when a time server is set), the same network interface all along, the
  24 V supply (board v1.1).
- **Live connection:** a WebSocket stays open the whole run and pings every 30 s; drops or more
  than 30 s without a status frame are warnings.
- **Doses:** per pump in automatic mode, every slot the schedule planned in the run (except the
  last 15 minutes) has its entry in the history: done (with the amount; less than the target is
  a warning), skipped with its reason (warning), failed or missing (fails). Doses without a
  slot are warnings, alarms of the run warnings or, for driver, clock and supply faults, failures.
  A dose may start up to the longest delay of the scheduler after its slot (engineering value,
  120 minutes when the script has no engineering key).
- **Hardware** (with `LGAL_ENG_KEY`): every fitted driver answers without a fault the whole run,
  error rate of both UART buses during the run, chip temperature, no history events dropped.

Sign-in and exit codes as in the acceptance test.

## API

All endpoints need a session or an API key (see [Access](#access)). Reachable without sign-in:
`/login`, `/setup`, `/api/v1/auth/status`, `/api/v1/auth/login`, `/api/v1/auth/claim`,
`/api/v1/auth/recover` and `GET /api/v1/system/language`. Before the initial setup is completed
only the setup is open (409 `setup_required`, see [Access](#access)). The `engineering` endpoints and all `/api/v1/hw/…` endpoints
additionally need the engineering unlock.

**Languages:** firmware, logs and API are English. Errors always come as
`{"error":{"code":"pump_busy","message":"Pump is busy"}}`; the `code` is fixed and is translated
by the pages (English default; German, French, Spanish, Italian, Dutch, Polish, Turkish,
Chinese – all but German machine-translated). The English texts of the web UI sit next to the code
(`add()` in each view); every other language is one file `components/webui/www/lang/<code>.json`
with all keys, loaded only when that language is used – a missing key shows in English. The
device pages (sign-in, setup, `/recovery`) carry their dictionaries inline, because `/recovery`
must work without the web UI. New language: entry in `settings::kLanguages` and in `languages`
of `lib/i18n.js`, the file in `lang/`, and the dictionaries in the device pages. The UI language
is a device setting, chosen with a picker that shows the flag of each language (`/flags.js`) on
the sign-in, setup and `/recovery` pages and in the settings of the web UI.

| Method | Path | Function |
|---|---|---|
| GET | `/` | web UI (M5); in factory state redirect to `/setup` |
| GET | `/recovery` | maintenance page (emergency, in the firmware image) |
| GET | `/ui/…` | files of the web UI (without sign-in) |
| GET | `/favicon.ico` | redirect to `/ui/icon.svg` (browsers ask for it on their own) |
| GET | `/apple-touch-icon.png`, `/apple-touch-icon-precomposed.png` | home screen icon (as `/ui/apple-touch-icon.png`, which every page names; Safari asks for these paths on its own) |
| GET | `/login` | sign-in, forgotten password (setup Wi-Fi), first own password |
| GET | `/api/v1/net/status` | active interface, LAN, Wi-Fi (state, RSSI, failure reason), setup Wi-Fi, `fallback_in_s` |
| GET / PUT | `/api/v1/net/settings` | settings (JSON, see [Network](#network-m4)); PUT takes partial documents |
| POST / GET | `/api/v1/net/scan` | start Wi-Fi scan / result (`idle\|running\|done\|failed`, strongest first) |
| POST / GET | `/api/v1/net/wlan/test` | connection test `{"ssid","password"?,"dhcp",…}` / result (`ok` with `ip`, `failed` with `reason`) |
| GET | `/api/v1/auth/status` | signed in?, password set?, this browser stays signed in (`remember`), how many do (`remembered`), engineering time left, recovery possible? |
| POST | `/api/v1/auth/login` | `{"password","remember"?}` → session cookie (`remember`: stay signed in for 90 days after the last use, also across restarts); without an admin password 409 `password_not_set` |
| POST | `/api/v1/auth/claim` | `{"device_password","new","language"?}` first admin password, only while none is set; no session |
| POST | `/api/v1/auth/logout` | end session |
| POST | `/api/v1/auth/logout_all` | end every session, stored ones included (session); API keys stay valid |
| POST | `/api/v1/auth/password` | `{"current","new"}` |
| POST | `/api/v1/auth/recover` | `{"device_password","new"}`, only from the setup Wi-Fi |
| GET / POST / DELETE | `/api/v1/auth/keys[?slot=1]` | list API keys / create `{"name"}` (after the initial setup) / delete |
| POST / DELETE | `/api/v1/auth/engineering` | `{"key"}` unlock / lock |
| GET | `/api/v1/system/info` | model and board, version, partition, OTA status, network, memory, time, language, links; `board_rev` (`"1.1"`) and on a board with 24 V sense `supply` (`{"state":"ok"\|"low"\|"missing","volts"}`) |
| GET / POST | `/api/v1/system/language?lang=en\|de` | UI language |
| GET / PUT | `/api/v1/system/led` | shown state (`shown`) and night mode / `{"night":{"enabled","start":"22:00","end":"07:00","brightness":0..100}}` (missing fields stay, 400 `invalid_param` with `field`) |
| GET / PUT | `/api/v1/system/time` | time, source, time zone, NTP server / `{"timezone"?,"ntp_server"?,"utc"?}` or instead of `utc` `"local":"2026-11-10T18:30"` (local time of the device; everything checked before anything is applied; 400 `invalid_param` with `field`) |
| GET | `/setup` | initial setup (page without sign-in) |
| GET / PUT | `/api/v1/system/device` | `name`, `hostname` (in use), `own_hostname` (`""` = from the name), `default_hostname` / `{"name","own_hostname"}` partially; 400 `invalid_param` with `field` |
| GET | `/api/v1/setup` | `completed`, `password_set`, `connected`, `hostname`, `urls` |
| POST | `/api/v1/setup/complete` | finish the setup (409 `password_not_set` / `not_connected`), setup Wi-Fi off after 20 s |
| GET | `/i18n.js` | shared translation base of the pages (without sign-in) |
| GET | `/flags.js` | flag icons of the languages and the styles of the language picker, for the pages and the web UI (without sign-in) |
| GET | `/api/v1/system/log` | RAM log of the last ~32 KB |
| POST | `/api/v1/system/reboot` | restart |
| GET / DELETE | `/api/v1/system/coredump` | last crash dump (ELF) / delete |
| POST | `/api/v1/system/factory_reset` | `{"password":"…"}` – factory reset with restart; only with a browser session (no API key) and the admin password, failed attempts count as for sign-in (403 `wrong_password`, 429 `too_many_attempts`) |
| POST | `/api/v1/ota/firmware` | firmware upload (raw `.bin`), rejected while a pump runs |
| GET | `/api/v1/ota/release` | last check of the update source: `{"source","channel"(null = no choice),"board","variant","current","auto_check","checking","checked_at"(null = not yet / clock unknown),"latest"(null = unknown),"available","notes"?,"error"?,"detail"?}` |
| POST | `/api/v1/ota/release/check` | check the source now → 202 `started` (result at `GET /api/v1/ota/release`); 400 `no_source` |
| POST | `/api/v1/ota/release/install` | install a newer version of the source → 202 `started`, progress at `GET /api/v1/ota/status`; errors there e.g. `no_update`, `sha256_mismatch`, `manifest_invalid`, `no_build`; 409 `pump_running` / `ota_in_progress` |
| POST | `/api/v1/ota/url` | engineering. `{"url":"https://…"}` → 202 `started`, the device downloads itself; 400 `invalid_url` / `http_not_allowed`, 409 `pump_running` / `ota_in_progress` |
| GET | `/api/v1/ota/status` | `{"state":"idle\|running\|failed\|rebooting","source":"api\|mqtt","http","received","total","error"?,"detail"?}` |
| GET / PUT | `/api/v1/ota/settings` | `{"source","channel","auto_check","allow_http"}`; `auto_check` `true` by default, `allow_http` `false` by default. `channel` `"stable"` (factory setting) or `"beta"`, `null` while there is no choice; 409 `no_channel` then. `source` and `allow_http` only change with engineering (`source` or `channel` changed → immediate check) |
| GET | `/api/v1/pumps` | status, calibration, retract, limit, container, tube of all pumps; `fitted` = fitted |
| POST | `/api/v1/pumps/dose?pump=1&ml=1.5` | dose (optional `tier=0..2`). During a lock time: 409 `collision_lock` with `retry_at`/`retry_local`, `pump` (holding the lock) and `ask`, or 202 `queued` (lock rule "queue"). With the rule "ask" the caller repeats it with `lock=queue` (wait) or `lock=override` (dose anyway); `lock=` is refused (400) with any other rule |
| POST | `/api/v1/pumps/stop?pump=0..5` | soft stop, the retract still runs (0 = all); discards a queued dose |
| POST | `/api/v1/pumps/prime?pump=1&action=start\|beat\|stop` | priming with dead-man heartbeat (better over WebSocket); `stop` ends priming only, a dose or calibration that runs keeps running (nothing to stop: 200 all the same) |
| GET | `/api/v1/ws` | WebSocket: status push and priming (see [WebSocket](#websocket-m4)) |
| POST | `/api/v1/pumps/calibrate?pump=1&action=start[&ml=10]` | calibration run; then `action=apply&measured_ml=…` or `cancel` |
| POST | `/api/v1/pumps/settings?pump=1&…` | `name`, `mode=auto\|manual_only\|disabled`, `tier`, `daily_limit_ml`, `retract_rev` (revolutions, `off` = 0 = off, `global` = service value; off by default), `direction=cw\|ccw` (direction seen from the front of the pump, set via `GCONF.shaft`, `cw` by default; only at standstill, otherwise 409 `pump_busy`), `warn_days`/`low_days` (own container thresholds in days of range, 0 = service default; alarm at most equal to warning), `remind_months` (calibration reminder 0–24 months, 0 = off) |
| POST | `/api/v1/pumps/container?pump=1&…` | `capacity_ml`, `remaining_ml` (capacity only = new, full container) |
| POST | `/api/v1/pumps/tube?pump=1&hours=0` | set/reset the tube counter; `hours=0` = new tube: `calibration.tube_changed` stays `true` until the next calibration |
| GET | `/api/v1/history?…` | events, newest first: `pump=1..5`, `kind=dose,skip,…`, `days=1..90`, or `from_date`/`to_date` (`YYYY-MM-DD`, local days, both inclusive, at most 90 days), or `from`/`to` in UTC seconds, `limit=1..500` (100), `offset` → `{"events":[…],"more"}` (see [History](#history-m6)) |
| GET | `/api/v1/history.csv?…` | the same filters as a CSV download, without limit |
| GET | `/api/v1/history/daily?days=14` | dosed amount, number of doses and skipped doses per local day and pump (1–90 days up to today, or `from_date`/`to_date` as above; oldest first; empty without a valid time) |
| GET / POST | `/api/v1/history/settings?keep_days=30` | retention `keep_days` (30–90, 90 by default; a shorter time from an older firmware or backup is raised to 30); changing only with engineering unlock |
| DELETE | `/api/v1/history` | delete the history and reset today's amounts and skipped doses (engineering) |
| GET | `/api/v1/backup?secrets=0\|1` | backup of the whole configuration (engineering, see [Backup](#backup-m6)) |
| POST | `/api/v1/backup?parts=system,network,pumps,engineering,mqtt,auth` | restore a backup (engineering); everything checked first, then the chosen parts applied; 409 `pump_busy` while a pump runs |
| GET / PUT | `/api/v1/mqtt` | MQTT settings (without password, `password_set`), base topic and port in use, `name_topic` (base topic with the current host name), connection status `disabled\|connecting\|connected` with the last error, CA info; PUT partial, leave out the password = keep, `""` = delete |
| PUT / DELETE | `/api/v1/mqtt/ca` | CA certificate(s) as PEM text in the body (max. 8 KB, checked) or remove (409 `ca_in_use` while TLS "own CA certificate" is active) |
| GET / POST | `/api/v1/engineering/dosing?…` | super admin: `retract_rev`, `calib_ml`, `tier1_rpm`…`tier3_rpm`, `prime_max_s`, `prime_heartbeat_ms`, `min_dose_ml`, `tube_life_h`, `limit_mode=calendar_day\|rolling_24h`, `warn_days` (warning when the container lasts fewer days, default 7), `low_days` (alarm "almost empty", default 4; 0 = off each, alarm at most equal to warning) |
| GET / PUT | `/api/v1/pumps/schedule?pump=1` | schedule of a pump (JSON, see below) + time grid `grid_min` + today's doses |
| POST | `/api/v1/pumps/boost?pump=1&ml=30&days=3` | start a boost, spread over `days=1..30` days or the next `doses=1..100` doses; `action=stop` ends it |
| GET | `/api/v1/schedule/status` | next dose (`ml` incl. the expected boost share, that as `boost_ml`), delay, queue, doses skipped today, boost (rest, `days` or `doses`, `doses_left`, `per_dose_ml`, `today_ml` = boost share of today's doses, start and end; with a dose window the end is its last dose), range of all pumps |
| GET / PUT | `/api/v1/schedule/locks` | lock times between pumps + rule for manual doses (JSON) |
| GET / POST | `/api/v1/engineering/scheduler?…` | super admin: `grid_min` (divisor of 1440, ≤ 60), `max_delay_min` |
| GET | `/api/v1/engineering/telemetry` | super admin: uptime, reset reason, chip temperature, heap (free, minimum, largest block, fragmentation), PSRAM, NVS entries, LittleFS and history, stack reserve per task (least stack ever left in bytes, tightest first) |
| POST | `/api/v1/engineering/reset?what=network` | super admin: reset the network settings (applies after 1 s); the factory reset is only on the maintenance page with the admin password |
| GET | `/api/v1/hw/status` | board (`id`, `rev`, `model`, `pumps`), drivers and pumps of the board, RTC, EEPROM, UART statistics; fitting `fitted` (now), `fitted_next` (after the next restart) and `fitted_factory` (factory population, `provision.py --pumps`) |
| POST | `/api/v1/hw/channels?pumps=1,2,3,4` | save the fitting (at least one pump), applies after a restart |
| POST | `/api/v1/hw/pump/run?pump=1&steps=±N&sps=N` | raw run (optional `accel`, `vstart`, `force=1`) |
| POST | `/api/v1/hw/pump/stop?pump=0..5&mode=soft\|hard` | stop (0 = all) |
| POST | `/api/v1/hw/pump/config?pump=1&run_ma=…` | `run_ma`, `hold_ma`, `microsteps`, `full_steps`, `invert`, `accel`, `vstart` |
| GET | `/api/v1/hw/tmc?pump=1` | TMC2209 raw registers |
| POST | `/api/v1/hw/time?utc=…` | set the time by hand (system + RTC) |
| POST | `/api/v1/hw/rtc/aging?offset=…` | DS3231 aging offset by hand (−128…127), only without time server (otherwise 409 `ntp_active`: automatic trimming) |
| POST | `/api/v1/hw/eeprom/test` | EEPROM write/read test |

Analysing a core dump:

```bash
idf.py coredump-info -c coredump.elf
```

## Hardware (M1)

| Assembly | Implementation |
|---|---|
| TMC2209 | UART on GPIO38/GPIO4 (TX/RX, pumps 1–4) and GPIO39/GPIO5 (pump 5), 115200 Bd; TX through 1 kΩ, RX directly on the bus. The echo of every request is checked and discarded; reply delay SENDDELAY = 24 bits (several drivers on one bus). Configuration: StealthChop, interpolation, current by register (Rsense 0.11 Ω), freewheeling at holding current 0. Checked via IFCNT, reconfiguration after a driver reset (GSTAT). |
| Step clocks | Pumps 1–4: RMT (exact step count by encoder). Pump 5: MCPWM + PCNT (counter stops at the target value). Trapezoidal ramp in integer arithmetic, all 5 in parallel, IRAM-safe. |
| EN | shared: on as soon as a pump runs; off 0.5 s after the last run (unless holding current > 0). |
| Monitoring | every 100 ms while running: SG_RESULT, DRV_STATUS (short circuit/over-temperature → stop). In turn every second: driver present, GSTAT. |
| Clock | The DS3231 stores UTC. Start: time from the RTC. NTP sets the RTC, phase-exact to the start of the second and only from 0.5 s deviation. In between, the firmware measures the deviation at the second edge on every NTP sync; after at least one day this gives the drift in ppm, which corrects the aging offset (about 0.1 ppm per step). Without a time server there is no reference: then the aging offset is set by hand in engineering; with a time server the firmware rejects a manual value (409 `ntp_active`). If the clock module had lost the time at start (oscillator stopped although the firmware had set the clock before), `rtc_lost` in `/api/v1/system/time` reports it: check the backup battery. Without NTP hourly sync from the RTC. POSIX time zone (`CET-1CEST,M3.5.0,M10.5.0/3`) and NTP server (default `pool.ntp.org`, empty = off) configurable via `/api/v1/system/time`, stored in NVS. |
| EEPROM | AT24C32 at 0x57 (address pins A0–A2 open) with page writes + ACK polling. From M2 the run state of the dosing (see below). At boot the log lists every I²C address that answers. |
| 24 V sense | Board v1.1 only (`provision.py --board-rev 1.1`; on v1.0 GPIO 6 floats and is never touched): GPIO 6 (ADC1, 12 dB, calibrated, mean of 16 samples) × 11. See [24 V supply](#24-v-supply-board-v11). |

### 24 V supply (board v1.1)

The ESP is powered from 24 V through the R-78E5.0, the board 3.3 V rail (drivers, W5500, clock
module) through the R-78E3.3. On USB power alone the rail is only back-fed through the I/O pins:
the modules then misbehave and the I²C bus times out. With the sense input the firmware checks
the supply before it touches them (`doser/supply.h`, thresholds in `doser/supply_core.h`,
host-tested):

| Supply | State | Behaviour |
|---|---|---|
| below 12 V | `missing` | at start: TMC2209, W5500 and DS3231/EEPROM are not initialised, EN is released (input) so it does not back-feed the rail; Wi-Fi, web UI and OTA run, the setup Wi-Fi opens for 30 min (also with a finished setup), the run state lives in NVS. Critical alarm `supply_missing`, every dose start → 409 `supply_missing`, scheduled doses are skipped with that reason. While running: pumps stop at once (lock), EN released, the job ends with `supply_missing`. |
| 12–20 V | `low` | normal start, warning `supply_low`; running doses finish, new ones → 409 `supply_low` (scheduled: skipped) |
| from 20 V | `ok` | normal operation |

Hysteresis 0.5 V on both thresholds; at runtime a change needs 3 readings in a row (every
0.5 s). When the supply comes back after it was missing (at start or later), the device restarts
on its own (not during an update), so the peripherals start cleanly. Without the board revision
(`board_rev` missing = v1.0) none of this runs and the device behaves as before.

The web UI shows the supply in the maintenance card ("Supply 24.1 V") and a banner while it is
missing or low; `/api/v1/system/info` has it as `supply`, MQTT as `supply_v` in `<base>/state`
with a diagnostic sensor "Supply voltage" in Home Assistant (board v1.1 only).

Defaults per pump (changeable in the engineering menu, stored in NVS): 600 mA run current,
0 mA holding current, 16 microsteps, 200 full steps/rev (step angle 1.8°), ramp 600 rpm per s,
start 20 rpm, max. 600 rpm.

Area D of the [engineering menu](#engineering-menu-m6) shows driver status, raw registers,
StallGuard and EEPROM self-test and moves single pumps by X steps. "Clock even without driver
answer" produces STEP pulses even without a responding TMC2209 (test with oscilloscope/logic
analyser).

## Dosing (M2)

| Topic | Implementation |
|---|---|
| Amount → steps | Calibration in µl per motor revolution (independent of the microsteps). Without calibration the nominal value 0.27 ml/rev (≈ 81 ml/min at 300 rpm; measured with water on board v1.0: 0.256 ml/rev, the nominal value lies a little above it so that an uncalibrated pump rather doses too little). |
| Sequence | Compensate retract → main run → anti-drip retract. The executed retract is stored safe against power loss. The retract is **off by default** per pump and can be set to "default" (global value) or an own value; when switched off, an open retract is still compensated on the next dose. |
| Booking | At start the full amount is booked to daily limit, container and total counter and saved; an abort credits the amount not delivered. The container never counts below 0 ml (forgotten refill); the dose still runs. Power cut during the run → the amount stays booked (safe side). |
| Daily limit | hard per pump; window calendar day or rolling 24 h (engineering). Booked in hour buckets over 26 h that start at the full hours of local time, so that the calendar day starts at midnight also with a time zone offset of hh:30 or hh:45. Rolling 24 h counts the hours started within the last 24 h: a dose drops out 23 to 24 h later, so that a plan whose daily amount equals the limit runs every day. Exception: on the day the clocks go forward, a dose comes 23 h after the one at the same time the day before; at the limit it is skipped. Without a valid time, pumps with a limit are locked. |
| Calibration | Run with a target amount (default 10 ml), then enter the measured amount. Plausible: 0.25 to 4 times the nominal value. The collected amount is subtracted from the container. Reminder per pump (`remind_months`, 0 = off, up to 24, default 3): if the last calibration is older, the pump reports `calibration.due` (only with a valid time). |
| Priming | runs as long as the button is held (heartbeat every 150 ms, stop after 500 ms without heartbeat or at once when the WebSocket connection drops, max. 60 s). Does not count for the container or the limit, but for the tube runtime. |
| Locks | mode `disabled`, OTA update, driver fault. |
| Storage | Settings in NVS (`dose`). Run state per pump in the EEPROM as A/B records (sequence number + CRC32, from 0x000), fallback to NVS without EEPROM. |

Engineering defaults (super admin, global; retract additionally per pump): retract "default"
¼ rev (≈ 0.07 ml), calibration target 10 ml, tiers 100/200/300 rpm (≈ 27/54/81 ml/min),
priming max. 60 s / heartbeat 500 ms, smallest dose 0.02 ml, tube life 1000 h, daily limit per
calendar day. Changes need the engineering unlock (see [Access](#access)).

The web UI operates this per pump; the engineering parameters are in the
[engineering menu](#engineering-menu-m6).

## Schedule (M3)

| Topic | Implementation |
|---|---|
| Plan | exactly one per pump: "spread" ("X ml in N doses between A and B", laid evenly onto the grid, rounding remainder on the first doses, at most 96 doses per day) or "single times" (up to 24 times, each with its own amount). Days and power-cut behaviour apply to the whole plan; whether it runs is decided by the pump's mode. |
| Days | daily, weekdays or "every N days" (1–30) from a start date. |
| Grid | Times on the schedule grid (default 5 min, engineering). Local time of the device time zone. |
| Daylight saving | Time zone as POSIX rule. A time in the spring gap is moved later; a double hour in autumn is dosed once (at its first occurrence). |
| Check on save | all pumps together: the runtime of each dose (amount, calibration, speed, ramp, retract + 2 s) must not reach the next dose of the same pump; lock times between pumps also across midnight and for interval days, counted from the full minute in which a dose ends (a dose of a few seconds at 08:00 and one at 08:30 fit a lock time of 30 min, a longer dose counts from its end); daily limit. Violations → 409 with a list of the colliding doses, nothing is saved. |
| Runtime protection | Every dose (also manual and boost) checks the lock times against the actual end of the last dose of the other pump, from the full minute of that end. A scheduled dose then waits and is discarded after `max_delay_min` (default 120 min). Priming and calibration are exempt. |
| Manual dose in a lock time | Rule `reject` (default): 409 with the time from which it is possible. Rule `queue`: queued (one per pump), starts after the lock time ends; a scheduled dose of the pump that comes due meanwhile waits behind it (at most `max_delay_min`). Rule `ask`: the web UI asks per dose – cancel, wait (queued as with `queue`) or dose anyway; a dose over a lock time names that pump in the history (`lock_overridden`, CSV column `value`). Home Assistant, MQTT and API calls without `lock=` are rejected as with `reject`. |
| Power cut | Up to 2 min late a dose counts as on time. After that it is skipped (default) or caught up within a configurable window. Skipped doses are counted per day and shown with a reason; after long outages everything older is skipped in one step. |
| Clock set back | By up to a day (e.g. NTP corrects a clock module that ran ahead): doses already given are not repeated. By more than a day, while running or found at start: the schedule starts again from now, and a dose waiting to start is dropped. |
| Boost | Extra amount over 1–30 days from the start (days × 24 h), spread evenly over all scheduled doses in this period: each dose gets rest ÷ doses still due. The share of a skipped or failed dose goes to the following ones; a rest after the last dose of the period is dropped. A changed schedule counts the doses again. Ends by itself. |
| Range | Container rest divided by the plan's average per day over one period of its days (a week for daily and weekdays, N days for "every N days"), so it does not change from day to day. The rest of a running boost comes off the container first; if the container runs dry before the boost ends, it lasts rest ÷ (plan per day + boost rest per remaining boost day). |
| Without a valid time | Nothing is scheduled; manual doses stay possible (except for pumps with a daily limit). |
| Storage | Plans, lock times, parameters in NVS (`sched`). Progress per pump (last processed dose, boost, counters) in the EEPROM as A/B records from 0x640, fallback to NVS. Written only when a dose was processed. |

Schedule as JSON (`PUT /api/v1/pumps/schedule?pump=1`; unknown fields are ignored, so a GET
result can be edited and sent back):

```json
{"kind": "spread", "start": "08:00", "end": "20:00", "count": 12, "ml": 24,
 "days": {"mode": "daily"}, "power_loss": "skip_missed", "catch_up_min": 60}

{"kind": "times", "times": [{"time": "09:00", "ml": 5}, {"time": "21:30", "ml": 2.5}],
 "days": {"mode": "interval", "every": 2, "anchor": "2026-11-06"}}

{"kind": "none"}
```

Lock times (`PUT /api/v1/schedule/locks`):

```json
{"manual": "reject", "rules": [{"pumps": [1, 2], "gap_min": 5}]}
```

`"end"` is only needed for more than one dose; single times are sorted on save. Errors name the
place (`"entry": 2, "field": "ml"` for the second time in sorted order) or, with 409 `conflict`,
the colliding doses (`"conflicts": [{"code": "collision_lock", "first": {…}, "second": {…}, "gap_min": 5}]`).
Points in time come as UTC seconds plus the local time of the device (`"at"` + `"local"`),
because the browser may be in another time zone.

The web UI edits schedules, lock times and boost (pump detail, settings), the scheduler
parameters the [engineering menu](#engineering-menu-m6).

## History (M6)

The device records events as 24-byte entries on the LittleFS partition `storage` (one file per
UTC day under `/storage/hist`, checksum per entry). Days older than the retention (engineering
menu, 30–90 days, 90 by default, part of the backup) are deleted hourly, after shortening within
a minute, and so are days after tomorrow, written while the clock was ahead. Writing goes through a queue in its own task – dosing and schedule never wait for the
flash. Without a valid time nothing is recorded. At around 240 doses a day that is about 6 KB
per day, 90 days ≈ 520 KB (storage 1.75 MB on N8R8, 5.75 MB on N16R8). The day files may take
three quarters of the partition at most; when a new entry would exceed that, or the flash is full,
the oldest days are deleted first – never the day of the new entry. A factory reset deletes
the history too; the engineering menu deletes it on its own and starts the amounts of today
(and with them the daily limit) and the count of skipped doses of today again.

| `kind` | Content |
|---|---|
| `dose`, `prime`, `calib_run` | target and actual amount, result (`success`, `aborted`, `fault`, `locked`, `supply_missing`, `heartbeat_lost`, `max_duration`), trigger (`schedule`, `manual`, `api`, `boost`, `mqtt`) |
| `skip` | skipped dose with reason (`missed`, `daily_limit`, `delay_timeout`, `supply_missing`, `supply_low`, …); after a power cut one entry with `count` |
| `container` | container set or refilled (`remaining_ml`, `capacity_ml`) |
| `calibration` | applied calibration (`ul_per_rev`, `measured_ml`, `target_ml`) |
| `tube` | tube counter set (`hours`) |
| `alarm` | start of an alarm: `container_low`, `tube_worn`, `rtc_fault`, `supply_missing`, `supply_low` (pump 0 = device); driver faults and daily limit are already in as result or skipped dose |
| `alarm_end` | end of an alarm, same codes |
| `boost` | `started` (`total_ml`, `days` or `doses`), `cancelled` (by hand, `done_ml` so far), `finished` (by itself: all doses given or period over, `done_ml`) |
| `mode` | mode changed (`mode`: `auto`, `manual_only`, `disabled`) |
| `schedule` | schedule changed (`set` with `daily_ml` and `doses` per day) or deleted (`cleared`); the same plan saved again is neither written to the flash nor recorded |
| `settings` | other pump settings changed; `changed` names the groups (`name`, `speed`, `retract`, `daily_limit`, `container`, `thresholds`, `calibration`, `reminder`, `direction`), with a changed direction also `direction` (`cw`/`ccw`, the new one) |
| `locks` | lock rules changed (pump 0, `rules`) |
| `system` | device (pump 0): `boot` (`reason`: `power_on`, `restart`, `crash`, `watchdog`, `brownout`, …), `updated` (`version`, `from`), `restored` (backup), `time_set`, `cleared` (history deleted – the first entry after it), `settings` (`area`: `language`, `time` – time zone or time server, `led`, `name`), `network`, `mqtt`, `channels` (`pumps`), `engineering`, `password`, `key_added`, `key_removed` |

`source` says who it was: `manual` = web UI (signed-in browser), `api` = API key, `mqtt` = Home
Assistant/MQTT, `schedule` = schedule (also the end of a boost), `boost` = dose with boost share;
without `source` the device noticed it itself (alarms, start). A dose triggered by hand in the
web UI is therefore `manual`. What a backup changes on restore is only in the history as
`system`/`restored`. Start and firmware change are added as soon as the time is valid (RTC at
once, otherwise after the first time sync), with the time of the start.

The CSV has the columns `utc,local,kind,pump,source,code,target_ml,done_ml,value`; `value` is
the content (container), µl per revolution (calibration), hours (tube), the count (skipped),
days or negative number of doses (boost), the mode (mode), doses per day (schedule), the number
of rules (lock rules), the changed groups joined with `+`, the direction as `direction(ccw)`
(settings), reset reason, version or the area of the settings (system). Entries written before
the groups and areas were recorded have none.

## MQTT / Home Assistant (M6)

Off by default. Enter a broker in the settings (pro); the device connects, registers a last
will and reconnects every 10 s after errors. Encryption:

| Setting | Use |
|---|---|
| none | port 1883, only in your own network |
| TLS, public certificate | port 8883, server certificate checked against the list of public certificate authorities included in the firmware (e.g. cloud brokers) |
| TLS, own CA certificate | port 8883, against an uploaded CA certificate (PEM, e.g. from your own Mosquitto with a self-signed certificate) |

Topics below the base topic (default: `lg-aquatic-life/<device-type>/<factory-hostname>`, e.g.
`lg-aquatic-life/doser/lgal-doser-e4f5`), states `retained`, events not. If the device gets a
name, the topic stays for now; "Move topics to the device name" (MQTT card, visible while the
topic differs from the name) sets it to `lg-aquatic-life/<device-type>/<hostname>`. After every
change of the base topic the device deletes the stored (`retained`) messages under the old one at
the broker. Client ID and Home Assistant IDs depend on the MAC and never change:

| Topic | Content |
|---|---|
| `<base>/status` | `online` / `offline` (last will) |
| `<base>/state` | `{"critical","warning","conditions":["pump1_container_low","rtc_fault","supply_missing",…]}`, on board v1.1 also `"supply_v"` (24 V supply in V, sent again from a change of 0.2 V) |
| `<base>/pump/<n>/state` | `{"activity","total_ml","planned_ml","remaining_ml","days","next","schedule","amount_ml","tube_h","last"}` – on change, otherwise every 5 min |
| `<base>/event` | every history event on its own (QoS 1), fields as `GET /api/v1/history` plus `"replay"`; see below |
| `<base>/pump/<n>/dose/set` | `PRESS` = configured amount, or amount in ml (`2.5`) – only with control |
| `<base>/pump/<n>/amount/set` | amount for `PRESS` in ml (in RAM, start 1 ml) – only with control |
| `<base>/pump/<n>/schedule/set` | `ON` / `OFF` – mode automatic / manual only (a pump switched off stays off) – only with control |
| `<base>/stop/set` | anything – stop all pumps – only with control |
| `<base>/update` | `{"installed_version","latest_version","release_url"?,"in_progress","update_percentage"}` for the update entity; `latest_version` = installed as long as no newer one is known |
| `<base>/update/set` | `install` – install a newer version of the [update source](#ota-update) (never a free URL) – only with control |
| `<base>/ota/state` | progress as `GET /api/v1/ota/status` (at start, every 5 s, at the end) |

Commands (`…/set`) count only when they arrive while the device is connected. A stored
(`retained`) command comes again after every connect and would repeat a dose, a stop or an update
each time; the device ignores it and logs a warning (delete it at the broker, e.g.
`mosquitto_pub -r -n -t <topic>`). Home Assistant sends its commands without retain.

**Home Assistant discovery** (on by default): a device "LGAL Doser XXXX" by device discovery
(`homeassistant/device/lgal_doser_xxxx/config`) with per pump status, total dosed
(`total_increasing`, Home Assistant builds the daily values), planned today, stock, range in
days, next dose and tube operating hours (diagnostic); plus alarm and warning of the device, on board v1.1 the
supply voltage (diagnostic) and – with an update source – the update
entity "Firmware" (installed and newer version, link to the release, progress). Names follow the
device language and the pump names. With **control**, a schedule switch, dose amount and "Dose"
per pump, "Stop all" and "Install" on the update entity are added (the latter also locked while a
pump runs); without control the device removes these entities again and ignores commands. When
Home Assistant restarts (`homeassistant/status` = `online`), the device sends discovery and
states again. Doses over MQTT run like manual doses (daily limit, lock times, container)
and appear with source `mqtt` in the history.

**Events** (`<base>/event`, function descriptions 1 and 9): all entries of the
[history](#history-m6) – doses, skipped doses, priming, calibration, container, tube, boost,
mode, schedule, settings, alarms and device events – go out in the order of the history, e.g.
`{"utc":1794330000,"local":"2026-11-10 18:20:00","kind":"dose","pump":2,"source":"schedule","code":"success","target_ml":1.5,"done_ml":1.5,"replay":false}`.
The device remembers how far it has sent (NVS, written at most once per minute). After an outage
of network or broker or a restart, the events buffered in the history (up to the retention)
follow with `"replay":true`, about 80 per second; the schedule keeps running unaffected. Delivery
at least once: after a restart the events of the last minute can come twice (`utc`, `pump`,
`kind` identify them). On the very first start the stream begins at the current time.

## Backup (M6)

**Schedule per pump** (everyone): export and import as a JSON file in the pump detail, also onto
another pump (`{"format":"lgal-doser-schedule","version":1,…,"schedule":{…}}`).

**Whole device** (engineering only, area F of the engineering menu): one file with device name
and own host name, language, time zone, time server, LED night mode, network, all pump settings
incl. calibration, schedules, lock times, service values, hardware parameters (currents,
microsteps, fitting – which applies after a restart), the history retention and
the MQTT settings including the CA certificate. Container content, counters and history are not
part of it. Selectable on export:

| Variant | Content |
|---|---|
| without passwords | no Wi-Fi and MQTT password, no credentials; on restore the stored Wi-Fi password stays if the network (SSID) is the same, and the MQTT password for the same broker and user |
| with passwords | additionally Wi-Fi and MQTT password (clear text) plus admin password and API keys (only as PBKDF2 or SHA-256 hash, as stored in the device) |

On restore the page shows device, firmware and date of the backup and lets you choose the parts;
network and credentials are deselected beforehand. The firmware checks the whole document
(format, version, every value like the individual endpoints) before applying anything, and
rejects it while a pump runs. Credentials sign everyone out; a new network applies 2 s after the
response. The engineering key (manufacturer) is never part of the backup.

## Engineering menu (M6)

Hidden expert menu for the manufacturer (`#/engineering`, function description 11): reachable
through the engineering dialog (click the firmware version in the footer 7×); every request
needs the engineering unlock. The page polls live values only while someone uses mouse or
keyboard – so the device locks 15 min after the last real input.

| Area | Content |
|---|---|
| A · Mechanics | Dosing physics for all pumps: retract "default", calibration target, three speed tiers (rpm with ml/min and steps/s). Drivers per pump: run and holding current, microsteps, full steps/rev, acceleration, start speed, direction; optionally for all fitted pumps. Fitting: which pumps are built in (at least one; on delivery and after a factory reset the factory population from `provision.py --pumps`, otherwise all pumps of the board); a pump not fitted is never addressed (no driver polling, no schedule, no alarm, dosing → 409 `pump_not_fitted`) and is hidden in the web UI and in Home Assistant; its settings are kept. Applies after a restart (saving restarts the device). |
| B · Limits | Priming max. duration and dead-man timeout, smallest single dose, daily limit window, schedule grid, max. delay of a scheduled dose. |
| C · Maintenance | Tube life, set/reset operating hours per pump. DS3231 temperature, aging offset: with a time server automatic trimming (measuring since, deviation now, last correction), without a time server input by hand, hint for a flat backup battery. History: recorded days and size, lost events, retention (30–90 days). |
| D · Diagnostics | Raw stepper test (steps forward/back, frequency, stop/immediate stop, live position and StallGuard; bypassing container, schedule and history). Driver diagnostics: status flags (short circuit, over-temperature, open coil, standstill, StealthChop, current scale) and TMC2209 raw registers. System telemetry incl. UART statistics, stack reserve per task and EEPROM self-test. Danger zone: delete history, reset network (the factory reset is on the maintenance page). |
| E · Firmware update | Update source (address of the release manifest, empty = none; saving checks at once) with module variant, allow unencrypted http (with warning); update from a URL with progress. |
| F · Backup | Back up and restore the whole device (see [Backup](#backup-m6)). |

The factory reset (maintenance page or button 10 s) stops all pumps, erases the EEPROM (run
state of dosing and schedule: counters, container, tube hours), NVS and the data partition and
restarts in setup mode.

## Button and LED

| Button (GPIO0) | Action on release | LED while holding |
|---|---|---|
| ≥ 3 s | setup Wi-Fi for 30 min (also to reset the admin password) | white |
| ≥ 10 s | factory reset (stop pumps, erase EEPROM, NVS and data partition) | red |

Pressed at power-on = ESP download mode (hardware). A button held down permanently triggers
nothing as long as it has not been released once.

LED states, highest priority first:

| Colour | Meaning |
|---|---|
| red, fast blinking | alarm: a scheduled dose skipped today because of the daily limit, last run with driver fault, clock module not answering, no 24 V supply (board v1.1) |
| white, fast blinking | firmware update running |
| magenta pulsing | calibration or priming |
| blue pulsing | pump running |
| white, slowly pulsing | setup Wi-Fi on |
| yellow, slow blinking | warning: container almost empty (lasts fewer days than the alarm threshold, default 4), tube life reached, no network, no valid time, 24 V supply below 20 V (board v1.1) |
| green | ready, connected via LAN or Wi-Fi |
| white fading in | start |

The LED uses only pure colours, so that every state stays clear also when the LED channels
differ: the colour says what it is about (red fault, yellow warning, green ready, blue pump
running, magenta work on a pump, white the device itself), the pattern how urgent it is. A pulse
starts bright and a blink starts on when the state appears, so a dose of 2 s shows as well. The
green chip of the light is far brighter than the red one (series resistors red 1 kΩ, green/blue
470 Ω), so yellow and white are mixed with less green: yellow with 15 % green duty, white with
30 % green and blue duty (set by eye on board d5v1, `led.cpp`).

Pumps switched off (mode "off") trigger neither alarm nor warning. The rule is in
`doser/alarms.h` (host-tested); `main.cpp` evaluates it every 2 s.

**Night mode** (settings → status LED or `PUT /api/v1/system/led`): dims the ready light
(green) in a time window of the local time, e.g. 22:00–07:00, or switches it off.
Alarms, warnings and running operations stay fully visible; without a valid time the light stays
bright. Off by default.

## Milestones

- [x] **M0** Skeleton: partitions, Ethernet, setup Wi-Fi, mDNS, OTA + rollback, maintenance page, LED, button
- [x] **M1** Hardware layer: TMC2209 UART, step clocks (RMT + MCPWM/PCNT), RTC, raw stepper test
- [x] **M2** Dosing engine: retract, calibration, priming, container, daily limit, tube runtime; languages (en/de)
- [x] **M3** Schedule, daylight saving, collision protection, boost, behaviour after a power cut (with host tests)
- [x] **M4** Wi-Fi client, captive portal, initial setup, auth, REST, WebSocket
- [x] **M5** Web UI (SPA, embedded in the firmware image)
- [x] **M6** MQTT + Home Assistant discovery, history, backup, engineering mode

All milestones are implemented and tested with host tests, fuzzers, the device mock and on board
v1.0.

## License

- **Firmware and other software:** GNU General Public License version 3 or later
  (GPL-3.0-or-later), full text in [LICENSE](../LICENSE). Copyright © 2026 Lars Greiss / LG
  Aquatic Life. Additional permission under GPLv3 section 7: the firmware may be conveyed
  together with the Espressif libraries published only as binaries (Wi-Fi, PHY, coexistence,
  ROM) – otherwise a GPL image with ESP-IDF Wi-Fi would strictly speaking not be distributable.
  The wording is at the start of [THIRD-PARTY-NOTICES.txt](../THIRD-PARTY-NOTICES.txt).
- **Hardware** (enclosure/STL/STEP/3MF, schematic, PCB layout, Gerber, bill of materials):
  Creative Commons BY-NC-SA 4.0, see [LICENSE-hardware.md](../LICENSE-hardware.md).
  Non-commercial use and sharing under the same terms with attribution; commercial use only with
  written permission.
- **Third-party software:** ESP-IDF, Espressif components (mdns, mqtt, w5500), Mbed TLS and htm
  under Apache 2.0; FreeRTOS, Preact, esp_littlefs, HTTP parser under MIT; lwIP, littlefs,
  wpa_supplicant, TLSF, Picolibc under BSD. All are compatible with the GPLv3. List and full texts
  are in [THIRD-PARTY-NOTICES.txt](../THIRD-PARTY-NOTICES.txt); `tools/third_party_notices.py`
  regenerates the file from the license files of ESP-IDF and the components (run it after every
  ESP-IDF or component update and commit the result).
- **On the device:** the firmware contains both texts; the maintenance page links them
  (`/ui/licenses/gpl-3.0.txt`, `/ui/licenses/third-party.txt`). So the notices also come along
  when only the .bin file is passed on, as MIT, BSD and Apache require.
