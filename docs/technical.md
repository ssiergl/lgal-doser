# LGAL-Doser – technical documentation

*English · [Deutsch](de/technik.md)*

Firmware of the "LG Aquatic Life" dosing pump (first device: ESP32-S3-DEV-KIT-N16R8,
5× TMC2209, W5500): structure, building, first start-up, interfaces and behaviour in detail.
Project overview: [README](../README.md); fixed hardware (pin assignment, circuit):
[CLAUDE.md](../CLAUDE.md).

Framework: **ESP-IDF v6.0.3**, C++ (gnu++2b), FreeRTOS.

## Structure

```
components/lgal_core/   LG Aquatic Life core: network, HTTP, OTA, LED, button, settings, log
components/doser_hal/   pin assignment, safe start, TMC2209 UART, step clocks, pumps, RTC/EEPROM
components/doser_app/   device logic: hardware diagnostics, dosing engine, scheduler, engineering and their REST APIs
components/webui/       web UI (M5): www/ (Preact + htm, no build), pack.py embeds it gzip-compressed
main/                   wiring of the components (app_main)
tools/                  provision.py, ap_password.py, ota_upload.py, release.sh, make_manifest.py, third_party_notices.py, mock/ (device mock for the web UI)
tests/host/             host tests (TMC protocol, current, ramp, calendar, time zones, JSON, dosing and schedule logic, LED alarms, history, backup, MQTT/Home Assistant, update manifest)
                        and simulation of dosing engine and scheduler (`tests/host/run.sh`)
tests/fuzz/             fuzzers for all parsers of external input (`tests/fuzz/run.sh`)
partitions*.csv         final partition tables per flash size (can only be changed over USB!)
variants/               configuration per module variant (N8R8, N16R8, N32R8)
.github/workflows/      CI (tests, fuzzers, all variants) and release build per git tag
docs/                   documentation, German versions in docs/de/
hardware/               enclosure and circuit board files (CC BY-NC-SA 4.0)
```

## Toolchain

```bash
git clone -b v6.0.3 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v6.0.3
```

```bash
~/esp/esp-idf-v6.0.3/install.sh esp32s3 && brew install cmake ninja
```

In every new shell:

```bash
source ~/esp/esp-idf-v6.0.3/export.sh
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

### Release

`tools/release.sh` builds all variants into `dist/`: per variant the app image
`lgal_doser-<V>.bin` (OTA) and `lgal_doser-<V>-full.bin` (bootloader, partition table,
OTA data and app for the first USB flash at address 0x0), plus `manifest.json`
(`tools/make_manifest.py`), `LICENSE` and `THIRD-PARTY-NOTICES.txt`.

On GitHub `.github/workflows/release.yml` does this: raise the version in `version.txt`, commit,
push the tag `v<version>`. The workflow checks that tag and `version.txt` match, builds and
publishes everything as a release. The images contain no secret (see
[First start-up](#first-start-up-once-over-usb-before-installation)), so the workflow needs no
repository secrets. Tags with a pre-release part (`v1.0.0-rc.1`) become pre-releases, which
`releases/latest` skips. The devices then use
`https://github.com/<owner>/<repo>/releases/latest/download/manifest.json` as update source.

`.github/workflows/ci.yml` runs the host tests under both sanitizers, a short fuzz run and the
build of all three variants on every push to `main` and every pull request (in the
`espressif/idf:v6.0.3` container).

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
   `python -m esptool --chip esp32s3 write-flash 0x0 lgal_doser-N16R8-full.bin`
   (it also overwrites `fctry`, so always do step 3 afterwards).

3. **Provision:** reads the MAC, writes the device secret
   HMAC-SHA256(master, "LGAL-DEVICE" + MAC) to `fctry` and shows the values for the rating
   plate:

   ```bash
   python3 tools/provision.py -p /dev/cu.usbmodem*
   ```

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

Sign-in on `/login` (page "Account"):
- **First sign-in** with the device password from the rating plate. Then the page asks for an
  own admin password (8–64 characters); from then on only that one is valid.
- **Sessions** live in RAM only (cookie `lgal_session`, max. 8, 7 days without use or 30 days
  absolute). Sign in again after a restart or OTA. Changing the password signs out all other
  browsers.
- **API keys** (max. 4) for apps, Home Assistant and scripts, header `X-API-Key: lgal_…` or
  `Authorization: Bearer lgal_…`. The key is shown only once; only its SHA-256 is stored. Keys
  survive restarts and password changes.
- **Failed attempts:** 5 free, then a wait per IP from 1 s, doubling up to 300 s. An IP is only
  forgotten after 1 h of quiet; if all 8 slots are taken, further IPs share one common wait
  (many addresses slow each other down).
- **Forgotten password:** hold the button for 3 s → setup Wi-Fi. Connected through it, `/login`
  can set a new admin password with the device password. Settings, schedules and calibration
  are kept.
- **Engineering** (super admin, manufacturer only): `/login?eng=1`, key per device derived
  from the device secret (`tools/ap_password.py --engineering`). It applies to the signed-in
  session and locks 15 min after the last engineering request. With an API key, additionally
  the header `X-Engineering-Key`. On start the web UI locks an unlock that does not come from
  the same browser tab – a closed tab therefore stays locked.

Browser pages without sign-in redirect to `/login?next=…`; API calls answer 401
`unauthorized` or 403 `engineering_locked`.

The device accepts changing requests (everything except GET) from a browser only from its own
pages (Origin = Host, otherwise 403 `forbidden_origin`), including sign-in, sign-out, password
change, API keys and engineering unlock. In addition to the `SameSite=Strict` cookie, this
protects against other devices under the same home domain. Clients without an Origin header
(Home Assistant, apps, scripts) are not affected.

In factory state (`setup_completed = false`) the setup Wi-Fi is always on and `/` leads to
the [initial setup](#initial-setup-m4).

## Initial setup (M4)

1. Connect to the setup Wi-Fi `LG-Aquatic-Life-Setup-XXXX` (password from the rating plate).
   The **captive portal** usually opens the setup on phones and laptops by itself; otherwise
   open `http://192.168.4.1/`.
2. Sign in with the device password; the page **`/setup`** guides through five steps:
   language → own admin password → network (LAN status, scan Wi-Fi, test, save) → time zone,
   NTP server, optionally time from the browser → finish.
3. **Finishing** is only possible with an admin password and a connected LAN or Wi-Fi. The page
   shows the new addresses (`http://lgal-doser-xxxx.local/` and IP); 20 s later the setup Wi-Fi
   switches off.

| Topic | Behaviour |
|---|---|
| Captive portal | DNS server on the setup Wi-Fi answers every name (A) with `192.168.4.1`, other types empty. Unknown pages redirect setup Wi-Fi clients to `http://192.168.4.1/`. LAN/Wi-Fi clients get a normal 404. |
| Repeat | `/setup` stays reachable after finishing (link on the maintenance page), but then does not switch the setup Wi-Fi on again. |
| Factory reset | Button 10 s, maintenance page (enter the admin password again) or engineering menu erases everything (see [Button and LED](#button-and-led)) → initial setup starts over. |

## Network (M4)

Page **`/network`**: status, settings, Wi-Fi scan and connection test.

| Topic | Behaviour |
|---|---|
| Interface | `auto` (default): LAN has priority. If the LAN has no address for 30 s (cable out, no DHCP), the Wi-Fi client starts; as soon as the LAN has an address again, the Wi-Fi radio goes off. `lan`: LAN only, Wi-Fi stays off. `wlan`: Wi-Fi only, Ethernet is switched off. |
| Addresses | LAN and Wi-Fi separately: DHCP or static (IP, mask /8–/30, gateway, DNS 1/2). The DNS servers follow the active interface. |
| Wi-Fi | WPA2 or newer (password 8–63 characters) or open. Reconnect with a wait of 2 s, doubling up to 60 s. |
| Applying | Saved at once (NVS `net`), applied 1 s after the response – otherwise the response would not reach the browser at a new IP. |
| Connection test | Checks SSID/password/addresses without saving (max. 25 s); afterwards the saved settings apply again. |
| Locked out? | Button 3 s → setup Wi-Fi, open `/network` there. The setup Wi-Fi runs in parallel to the Wi-Fi client. |

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
| Status push | `{"type":"status","seq","utc","time_valid","pumps":[…]}` right after connecting, on every change (checked every 250 ms) and at least every 10 s. Per pump only the live values: activity, phase, progress (`done_ml`, `target_ml`, `rpm`, `prime_s`), container, daily amount, last run. Everything else comes from `GET /api/v1/pumps`. |
| Requests | `{"type":"…","id"?:n,…}` → answer with the same `type`/`id`, `"ok"` and on error `"error"` (code). `{"type":"ping"}` for testing. |
| Priming | `{"type":"prime","pump":1,"action":"start\|beat\|stop","tier"?:0..2}`. While holding, `beat` every 150 ms. If the connection drops, the pump stops at once (additionally after `prime_heartbeat_ms` without heartbeat). |
| Security | Browsers only from the device's own pages (Origin = Host, otherwise 403 `forbidden_origin`). Max. 4 connections (503 `too_many_clients`). Sign-out, password change or a deleted API key end the connection (checked every 5 s and on every message). |

The web UI uses the WebSocket for live values and priming and falls back to polling and REST
priming without it.

## Web UI (M5)

`http://lgal-doser-xxxx.local/` opens the UI (sign-in as under [Access](#access)). It is
embedded in the firmware: `pack.py` gzip-compresses `components/webui/www/` at build time
(≈ 104 KB), and it is updated with the firmware over OTA – UI and API thus always match, and the
rollback covers the UI as well. Preact and htm sit unchanged in `www/vendor/` (versions and
checksums in the README there); there is no Node build, `idf.py build` is enough.

| Topic | Behaviour |
|---|---|
| Pages | Overview, pump (detail with schedule), history, settings, maintenance (`/#/maintenance`, from the settings). Addresses as hash routes, e.g. `/#/pump/3` or `/#/history/3` (events pre-filtered to pump 3); old links `/#/schedule/3` open the pump. |
| Overview | Two layouts: from 940 px width (desktop, tablet landscape) two columns, the fifth card equally wide at the bottom left; below that one column (mobile, tabs as a bar at the bottom), on very narrow phones long values wrap. Per pump: container graphic with level (border yellow if it lasts fewer days than the warning threshold, default 7, red below the alarm threshold, default 4; both thresholds configurable per pump, dashed without container tracking), dosed / planned today (turquoise with boost, incl. the boost share of today's doses), range in days (from the schedule), next dose (highlighted with boost), running dose. Next to the status only small icon badges with counts for errors and warnings plus a boost icon (text as tooltip), so that all cards stay the same height; the texts (driver fault in the last run, container almost empty, calibration missing or too old, daily limit, tube, lock time, skipped doses) are in the pump view. |
| Pump | Container card as summary, the same size for all pumps, below it one card each for alarms (red) and warnings (yellow) with the texts of the overview badges, only if there are any: large container graphic, dosed / planned today, range, next dose (with boost share; below it "incl. boost (+x ml per dose)" in turquoise), plus "Refilled" and "Change" (size 0 = no tracking); dose now with progress and stop (lock time: message with end or "starts afterwards"), prime tube by holding (WebSocket with dead-man, otherwise REST), calibration wizard (start dialog asks whether the tube is primed and primes it there by holding → measuring cup → apply or discard the measured amount), usage (daily limit, last run, total, tube hours; "Tube replaced" only in pro – afterwards "New tube – calibrate again" shows as a warning at the pump until it is calibrated) and settings. Name, mode (automatic / manual only / off) and calibration reminder (off, 3, 6, 9 or 12 months, default 3) for everyone; speed when dosing, target amount of the calibration and fine-tuning only in pro. Simple shows active pro values as a hint. |
| Schedule | Card in the pump detail: today's doses on a 24 h bar with the current time, below it the plan – exactly one per pump. **Simple:** daily amount in N doses between two times, daily, with a preview of the times; a plan created in pro is only summarised and stays active (edit it in pro or replace it with a simple plan). **Pro:** plan kind "spread" (daily amount in N doses) or "single times" (up to 24 times with their own amount), daily / weekdays / every N days, and the behaviour after a power cut. Whether the plan runs is decided by the pump's mode (automatic / manual only). The bar shows unsaved changes at once; grid, daily limit and lock time conflicts are reported with the time or pumps and times. In pro: export the pump's schedule as a JSON file and import it (also on another pump); the device checks the imported plan like an edited one. |
| History | For everyone: dosed amount per day as a small chart per pump (7, 14, 30 or 60 days), as line (default) or bars (remembered in the browser), with labelled axes (ml; weekdays or date, "today" always labelled), sum and daily average; pointing or tapping names day, amount, number of doses and skipped doses (dot below the axis), plus a table view. Pro only: event list (doses, skipped doses, priming, calibration, container, tube, alarms) with filter by pump, kind and period, "Load more" and CSV export. The pump detail links to the pump's history under "Usage". |
| Boost | Card in the pump detail (simple and pro): extra amount over 1–30 days with the preview "≈ X ml extra per day", in pro optionally over the next 1–100 scheduled doses (preview "≈ X ml extra per dose", ends with the last of them), hint when there is no schedule, mode ≠ automatic or the daily limit would be exceeded. A running boost shows the rest, progress, end (day and time) and the remaining doses with their share, and can be ended; overview and header show it as well. |
| Lock times | Pro only, in the settings: pump pairs with a minimum gap in minutes (up to 10 rules) and whether a manual dose during the lock time is rejected or waits. On save the device checks all schedules against the rules and reports collisions. |
| Time | In the settings: device time and source (time server, clock module, by hand), warning for more than 2 min difference to the browser, a silent clock module or a lost time (backup battery), "Take the time from this browser". Time zone (list as in the setup wizard or POSIX string, suggestion from the browser) for everyone; time server (NTP) and last sync only in pro; date and time by hand (pro) only without time server – with a time server the device sets the clock itself. |
| Status LED | In the settings for everyone: what the LED shows right now, the meaning of the colours and the night mode (time window, ready light at night off / 10 / 25 / 50 %). |
| Network | In the settings: status (active interface, LAN and Wi-Fi address, signal, setup Wi-Fi, countdown to the Wi-Fi fallback) for everyone. Pro only: interface (automatic / LAN only / Wi-Fi only), DHCP or static addresses per interface (prefilled with the current ones), Wi-Fi scan, password, connection test and save; if the static LAN address changes, the page names the new one. `/network` stays as an emergency page. |
| MQTT | In the settings: connection status with the last error (host name, unreachable, TLS, sign-in) for everyone. Pro only: broker and port, encryption (none / TLS with public certificate / TLS with own CA certificate incl. upload, replace, remove), user and password, base topic, Home Assistant discovery with prefix, "Allow control via MQTT" with a warning and the list of topics. See [MQTT / Home Assistant](#mqtt--home-assistant-m6). |
| Sign-in | In the settings: change the password (or set it for the first time) and sign out for everyone; API keys (list, create with one-time display and copy, delete) only in pro. `/login` stays for sign-in, forgotten password and engineering. |
| Maintenance | Firmware version, build date, address, uptime and a hint after a rollback for everyone; only with an [update source](#ota-update) the card "Updates": installed and available version (badge "Up to date" / "New: x.y.z"), time of the last check, link "What's new", "Check for updates", "Install x.y.z" with confirmation and progress, switch "Check daily"; firmware update by `.bin` upload with progress (locked while a pump runs), afterwards the page waits for the restart and reloads; restart with confirmation; factory reset with the admin password entered again; licenses (GPLv3 and third-party software). Pro only: warning while http is allowed for updates, storage and OTA state, MAC, last reset reason, memory, log and crash report (download, delete). |
| View | **Simple** / **Pro**, stored per browser. Only in pro: detailed schedules, pump fine-tuning (speed, retract, daily limit), rules between pumps (lock times, queueing, power cut) plus network and MQTT settings, time server and API keys. What is set in pro stays active in simple. |
| Live values | Over WebSocket; without a connection the page polls every second. |
| Device | In the settings: device name (up to 32 bytes, e.g. "Reef tank") for header, Home Assistant and mDNS service. The host name follows from it (lower case, umlauts as ae/oe/ue/ss, otherwise "-", e.g. `reef-tank.local`); pro only: own host name (a–z, 0–9, "-", up to 32 characters). Without a name it stays `lgal-doser-xxxx`. mDNS takes the name at once, the router at the next address assignment. MQTT topics and Home Assistant IDs stay until "Move topics to the device name" is pressed in the MQTT card. |
| Appearance | Dark, light or as the system; language as on all device pages; time format 24 hours (default) or 12 hours with AM/PM for all shown times (remembered in the browser; time input fields follow the browser). In the background a coral reef in three depths, drawn by `lib/reef.js` at load from a fixed seed (the same picture every time, ≈ 3 KB instead of ≈ 130 KB of SVG); seaweed sways, bubbles rise – off with "reduce motion". |
| Service | Click the firmware version in the footer 7× quickly → dialog for the engineering key, then straight to the [engineering menu](#engineering-menu-m6) (at once if it is already unlocked). The [backup](#backup-m6) and "Lock now" are there too. |
| Emergency | `/recovery` stays as a maintenance page without the web UI (firmware upload, restart, log, links to network and setup) in case the UI does not load. It is deliberately not linked anywhere: type the address by hand, e.g. `http://lgal-doser-xxxx.local/recovery` (the user guide mentions it). |
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
`test-admin-pw`, engineering key `test-eng-key`. The state is lost on exit.

## OTA update

In the web UI under maintenance, via the emergency page (`/recovery`) by file upload, or by
script:

```bash
python3 tools/ota_upload.py lgal-doser-xxxx.local --key lgal_...
```

Sign-in: `--key` (or `LGAL_API_KEY`), `--password` (or `LGAL_PASSWORD`, admin password) or,
before the initial setup, `--mac AA:BB:CC:DD:EE:FF` (device password). With a password the script
signs in again by itself after the restart.

Procedure and protection:
- The image is checked before writing (ESP32-S3, project name `lgal_doser`, flash size) and
  verified by hash after writing.
- The new image starts as `PENDING_VERIFY` and is only marked valid after 60 s of stable running
  with a reachable web server.
- Crash, watchdog or failed health check before that: automatic **rollback** to the previous
  firmware.
- The pump drivers are switched off for the duration of the update.

**Update source:** a fixed address of the release manifest, e.g.
`https://github.com/<owner>/<repo>/releases/latest/download/manifest.json`. Factory value from
`CONFIG_LGAL_OTA_SOURCE` (empty = no source); it can only be changed in the
[engineering menu](#engineering-menu-m6). The manifest names the version and, per module
variant (`N8R8`, `N16R8`, `N32R8`, by flash size of the build), file, SHA-256 and optionally
size:

```json
{"version":"0.2.0","notes":"https://github.com/<owner>/<repo>/releases/tag/v0.2.0",
 "builds":[{"variant":"N16R8","url":"lgal_doser-N16R8.bin","sha256":"<64 hex characters>","size":1234567}]}
```

- `url` absolute or relative to the configured manifest address (not to the target of a
  redirect); `notes` is only taken as an http(s) address. Manifest at most 4 KB.
- Versions follow Semantic Versioning; a pre-release (`1.0.0-rc.1`) is older than `1.0.0`. Only
  a version newer than the running one is offered.
- **Automatic check** (on by default): about 10 min after start, then daily, hourly after an
  error. It only reports – nothing is ever installed without an order.
- Installing loads the manifest once more, then the file of the device's own variant; if SHA-256
  or size do not match, the image is discarded before it becomes active. Otherwise as with an
  upload (locked while a pump runs; rollback protection).
- Source and file follow the https and http rules of the update from a URL.

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

## API

All endpoints need a session or an API key (see [Access](#access)). Reachable without sign-in:
`/login`, `/api/v1/auth/status`, `/api/v1/auth/login`, `/api/v1/auth/recover` and
`GET /api/v1/system/language`. The `engineering` endpoints and all `/api/v1/hw/…` endpoints
additionally need the engineering unlock.

**Languages:** firmware, logs and API are English. Errors always come as
`{"error":{"code":"pump_busy","message":"Pump is busy"}}`; the `code` is fixed and is translated
by the pages (English default, German; more languages: entry in `settings::kLanguages` +
dictionaries in `i18n.js` and the pages). The UI language is a device setting and can be switched
on every page.

| Method | Path | Function |
|---|---|---|
| GET | `/` | web UI (M5); in factory state redirect to `/setup` |
| GET | `/recovery` | maintenance page (emergency, in the firmware image) |
| GET | `/ui/…` | files of the web UI (without sign-in) |
| GET | `/login` | sign-in, password, API keys, engineering |
| GET | `/network` | network page |
| GET | `/api/v1/net/status` | active interface, LAN, Wi-Fi (state, RSSI, failure reason), setup Wi-Fi, `fallback_in_s` |
| GET / PUT | `/api/v1/net/settings` | settings (JSON, see [Network](#network-m4)); PUT takes partial documents |
| POST / GET | `/api/v1/net/scan` | start Wi-Fi scan / result (`idle\|running\|done\|failed`, strongest first) |
| POST / GET | `/api/v1/net/wlan/test` | connection test `{"ssid","password"?,"dhcp",…}` / result (`ok` with `ip`, `failed` with `reason`) |
| GET | `/api/v1/auth/status` | signed in?, password set?, engineering time left, recovery possible? |
| POST | `/api/v1/auth/login` | `{"password"}` → session cookie |
| POST | `/api/v1/auth/logout` | end session |
| POST | `/api/v1/auth/password` | `{"current","new"}` (without `current` before it is first set) |
| POST | `/api/v1/auth/recover` | `{"device_password","new"}`, only from the setup Wi-Fi |
| GET / POST / DELETE | `/api/v1/auth/keys[?slot=1]` | list API keys / create `{"name"}` / delete |
| POST / DELETE | `/api/v1/auth/engineering` | `{"key"}` unlock / lock |
| GET | `/api/v1/system/info` | version, partition, OTA status, network, memory, time, language, links |
| GET / POST | `/api/v1/system/language?lang=en\|de` | UI language |
| GET / PUT | `/api/v1/system/led` | shown state (`shown`) and night mode / `{"night":{"enabled","start":"22:00","end":"07:00","brightness":0..100}}` (missing fields stay, 400 `invalid_param` with `field`) |
| GET / PUT | `/api/v1/system/time` | time, source, time zone, NTP server / `{"timezone"?,"ntp_server"?,"utc"?}` or instead of `utc` `"local":"2026-11-10T18:30"` (local time of the device; everything checked before anything is applied; 400 `invalid_param` with `field`) |
| GET | `/setup` | initial setup |
| GET / PUT | `/api/v1/system/device` | `name`, `hostname` (in use), `own_hostname` (`""` = from the name), `default_hostname` / `{"name","own_hostname"}` partially; 400 `invalid_param` with `field` |
| GET | `/api/v1/setup` | `completed`, `password_set`, `connected`, `hostname`, `urls` |
| POST | `/api/v1/setup/complete` | finish the setup (409 `password_not_set` / `not_connected`), setup Wi-Fi off after 20 s |
| GET | `/i18n.js` | shared translation base of the pages (without sign-in) |
| GET | `/api/v1/system/log` | RAM log of the last ~32 KB |
| POST | `/api/v1/system/reboot` | restart |
| GET / DELETE | `/api/v1/system/coredump` | last crash dump (ELF) / delete |
| POST | `/api/v1/system/factory_reset` | `{"password":"…"}` – factory reset with restart; only with a browser session (no API key) and the admin password, failed attempts count as for sign-in (403 `wrong_password`, 429 `too_many_attempts`) |
| POST | `/api/v1/ota/firmware` | firmware upload (raw `.bin`), rejected while a pump runs |
| GET | `/api/v1/ota/release` | last check of the update source: `{"source","variant","current","auto_check","checking","checked_at"(null = not yet / clock unknown),"latest"(null = unknown),"available","notes"?,"error"?,"detail"?}` |
| POST | `/api/v1/ota/release/check` | check the source now → 202 `started` (result at `GET /api/v1/ota/release`); 400 `no_source` |
| POST | `/api/v1/ota/release/install` | install a newer version of the source → 202 `started`, progress at `GET /api/v1/ota/status`; errors there e.g. `no_update`, `sha256_mismatch`, `manifest_invalid`, `no_build`; 409 `pump_running` / `ota_in_progress` |
| POST | `/api/v1/ota/url` | engineering. `{"url":"https://…"}` → 202 `started`, the device downloads itself; 400 `invalid_url` / `http_not_allowed`, 409 `pump_running` / `ota_in_progress` |
| GET | `/api/v1/ota/status` | `{"state":"idle\|running\|failed\|rebooting","source":"api\|mqtt","http","received","total","error"?,"detail"?}` |
| GET / PUT | `/api/v1/ota/settings` | `{"source","auto_check","allow_http"}`; `auto_check` `true` by default, `allow_http` `false` by default. `source` and `allow_http` only change with engineering (`source` set → immediate check) |
| GET | `/api/v1/pumps` | status, calibration, retract, limit, container, tube of all pumps; `fitted` = fitted |
| POST | `/api/v1/pumps/dose?pump=1&ml=1.5` | dose (optional `tier=0..2`). During a lock time: 409 `collision_lock` with `retry_at`/`retry_local`, or 202 `queued` (lock rule "queue") |
| POST | `/api/v1/pumps/stop?pump=0..5` | soft stop, the retract still runs (0 = all); discards a queued dose |
| POST | `/api/v1/pumps/prime?pump=1&action=start\|beat\|stop` | priming with dead-man heartbeat (better over WebSocket) |
| GET | `/api/v1/ws` | WebSocket: status push and priming (see [WebSocket](#websocket-m4)) |
| POST | `/api/v1/pumps/calibrate?pump=1&action=start[&ml=10]` | calibration run; then `action=apply&measured_ml=…` or `cancel` |
| POST | `/api/v1/pumps/settings?pump=1&…` | `name`, `mode=auto\|manual_only\|disabled`, `tier`, `daily_limit_ml`, `retract_rev` (revolutions, `off` = 0 = off, `global` = service value; off by default), `direction=cw\|ccw` (direction via `GCONF.shaft`, `cw` by default; only at standstill, otherwise 409 `pump_busy`), `warn_days`/`low_days` (own container thresholds in days of range, 0 = service default; alarm at most equal to warning), `remind_months` (calibration reminder 0–24 months, 0 = off) |
| POST | `/api/v1/pumps/container?pump=1&…` | `capacity_ml`, `remaining_ml` (capacity only = new, full container) |
| POST | `/api/v1/pumps/tube?pump=1&hours=0` | set/reset the tube counter; `hours=0` = new tube: `calibration.tube_changed` stays `true` until the next calibration |
| GET | `/api/v1/history?…` | events, newest first: `pump=1..5`, `kind=dose,skip,…`, `days=1..60` (or `from`/`to` in UTC seconds), `limit=1..500` (100), `offset` → `{"events":[…],"more"}` (see [History](#history-m6)) |
| GET | `/api/v1/history.csv?…` | the same filters as a CSV download, without limit |
| GET | `/api/v1/history/daily?days=14` | dosed amount, number of doses and skipped doses per local day and pump (1–60 days, oldest first; empty without a valid time) |
| GET / POST | `/api/v1/history/settings?keep_days=30` | retention `keep_days` (1–60, 60 by default); changing only with engineering unlock |
| DELETE | `/api/v1/history` | delete the history (engineering) |
| GET | `/api/v1/backup?secrets=0\|1` | backup of the whole configuration (engineering, see [Backup](#backup-m6)) |
| POST | `/api/v1/backup?parts=system,network,pumps,engineering,mqtt,auth` | restore a backup (engineering); everything checked first, then the chosen parts applied; 409 `pump_busy` while a pump runs |
| GET / PUT | `/api/v1/mqtt` | MQTT settings (without password, `password_set`), base topic and port in use, `name_topic` (base topic with the current host name), connection status `disabled\|connecting\|connected` with the last error, CA info; PUT partial, leave out the password = keep, `""` = delete |
| PUT / DELETE | `/api/v1/mqtt/ca` | CA certificate(s) as PEM text in the body (max. 8 KB, checked) or remove (409 `ca_in_use` while TLS "own CA certificate" is active) |
| GET / POST | `/api/v1/engineering/dosing?…` | super admin: `retract_rev`, `calib_ml`, `tier1_rpm`…`tier3_rpm`, `prime_max_s`, `prime_heartbeat_ms`, `min_dose_ml`, `tube_life_h`, `limit_mode=calendar_day\|rolling_24h`, `warn_days` (warning when the container lasts fewer days, default 7), `low_days` (alarm "almost empty", default 4; 0 = off each, alarm at most equal to warning) |
| GET / PUT | `/api/v1/pumps/schedule?pump=1` | schedule of a pump (JSON, see below) + time grid `grid_min` + today's doses |
| POST | `/api/v1/pumps/boost?pump=1&ml=30&days=3` | start a boost, spread over `days=1..30` days or the next `doses=1..100` doses; `action=stop` ends it |
| GET | `/api/v1/schedule/status` | next dose (`ml` incl. the expected boost share, that as `boost_ml`), delay, queue, skipped, boost (rest, `days` or `doses`, `doses_left`, `per_dose_ml`, `today_ml` = boost share of today's doses, start and end; with a dose window the end is its last dose), range of all pumps |
| GET / PUT | `/api/v1/schedule/locks` | lock times between pumps + rule for manual doses (JSON) |
| GET / POST | `/api/v1/engineering/scheduler?…` | super admin: `grid_min` (divisor of 1440, ≤ 60), `max_delay_min` |
| GET | `/api/v1/engineering/telemetry` | super admin: uptime, reset reason, chip temperature, heap (free, minimum, largest block, fragmentation), PSRAM, NVS entries, LittleFS and history |
| POST | `/api/v1/engineering/reset?what=network` | super admin: reset the network settings (applies after 1 s); the factory reset is only on the maintenance page with the admin password |
| GET | `/api/v1/hw/status` | drivers, pumps, RTC, EEPROM, UART statistics; fitting `fitted` (now) and `fitted_next` (after the next restart) |
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
| TMC2209 | Single-wire UART on GPIO38 (pumps 1–4) / GPIO39 (pump 5), 115200 Bd. The echo is checked; for the answer the pin is released (SENDDELAY = 24 bits). Configuration: StealthChop, interpolation, current by register (Rsense 0.11 Ω), freewheeling at holding current 0. Checked via IFCNT, reconfiguration after a driver reset (GSTAT). |
| Step clocks | Pumps 1–4: RMT (exact step count by encoder). Pump 5: MCPWM + PCNT (counter stops at the target value). Trapezoidal ramp in integer arithmetic, all 5 in parallel, IRAM-safe. |
| EN | shared: on as soon as a pump runs; off 0.5 s after the last run (unless holding current > 0). |
| Monitoring | every 100 ms while running: SG_RESULT, DRV_STATUS (short circuit/over-temperature → stop). In turn every second: driver present, GSTAT. |
| Clock | The DS3231 stores UTC. Start: time from the RTC. NTP sets the RTC, phase-exact to the start of the second and only from 0.5 s deviation. In between, the firmware measures the deviation at the second edge on every NTP sync; after at least one day this gives the drift in ppm, which corrects the aging offset (about 0.1 ppm per step). Without a time server there is no reference: then the aging offset is set by hand in engineering; with a time server the firmware rejects a manual value (409 `ntp_active`). If the clock module had lost the time at start (oscillator stopped although the firmware had set the clock before), `rtc_lost` in `/api/v1/system/time` reports it: check the backup battery. Without NTP hourly sync from the RTC. POSIX time zone (`CET-1CEST,M3.5.0,M10.5.0/3`) and NTP server (default `pool.ntp.org`, empty = off) configurable via `/api/v1/system/time`, stored in NVS. |
| EEPROM | AT24C32 with page writes + ACK polling. From M2 the run state of the dosing (see below). |

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
| Amount → steps | Calibration in µl per motor revolution (independent of the microsteps). Without calibration the nominal value 0.37 ml/rev (≈ 111 ml/min at 300 rpm). |
| Sequence | Compensate retract → main run → anti-drip retract. The executed retract is stored safe against power loss. The retract is **off by default** per pump and can be set to "default" (global value) or an own value; when switched off, an open retract is still compensated on the next dose. |
| Booking | At start the full amount is booked to daily limit, container and total counter and saved; an abort credits the amount not delivered. The container never counts below 0 ml (forgotten refill); the dose still runs. Power cut during the run → the amount stays booked (safe side). |
| Daily limit | hard per pump; window calendar day or rolling 24 h (engineering). Hour buckets over 26 h, started hours count fully. Without a valid time, pumps with a limit are locked. |
| Calibration | Run with a target amount (default 10 ml), then enter the measured amount. Plausible: 0.25 to 4 times the nominal value. The collected amount is subtracted from the container. Reminder per pump (`remind_months`, 0 = off, up to 24, default 3): if the last calibration is older, the pump reports `calibration.due` (only with a valid time). |
| Priming | runs as long as the button is held (heartbeat every 150 ms, stop after 500 ms without heartbeat or at once when the WebSocket connection drops, max. 60 s). Subtracted from the container with the nominal flow (or calibration), but does not count as a dose (daily amount, limit, total counter); counts for the tube runtime. |
| Locks | mode `disabled`, OTA update, driver fault. |
| Storage | Settings in NVS (`dose`). Run state per pump in the EEPROM as A/B records (sequence number + CRC32, from 0x000), fallback to NVS without EEPROM. |

Engineering defaults (super admin, global; retract additionally per pump): retract "default"
¼ rev (≈ 0.09 ml), calibration target 10 ml, tiers 100/200/300 rpm (≈ 37/74/111 ml/min),
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
| Check on save | all pumps together: the runtime of each dose (amount, calibration, speed, ramp, retract + 2 s) must not reach the next dose of the same pump; lock times between pumps also across midnight and for interval days; daily limit. Violations → 409 with a list of the colliding doses, nothing is saved. |
| Runtime protection | Every dose (also manual and boost) checks the lock times against the actual end of the last dose of the other pump. A scheduled dose then waits and is discarded after `max_delay_min` (default 120 min). Priming and calibration are exempt. |
| Manual dose in a lock time | Rule `reject` (default): 409 with the time from which it is possible. Rule `queue`: queued (one per pump), starts after the lock time ends. |
| Power cut | Up to 2 min late a dose counts as on time. After that it is skipped (default) or caught up within a configurable window. Skipped doses are counted and shown with a reason; after long outages everything older is skipped in one step. |
| Boost | Extra amount over 1–30 days from the start (days × 24 h), spread evenly over all scheduled doses in this period: each dose gets rest ÷ doses still due. The share of a skipped or failed dose goes to the following ones; a rest after the last dose of the period is dropped. A changed schedule counts the doses again. Ends by itself. |
| Range | Container rest divided by the average of the next 7 days (incl. boost). |
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
menu, 1–60 days, 60 by default, part of the backup) are deleted hourly, after shortening within
a minute. Writing goes through a queue in its own task – dosing and schedule never wait for the
flash. Without a valid time nothing is recorded. At around 240 doses a day that is about 6 KB
per day, 60 days ≈ 350 KB (storage 1.75 MB on N8R8, 5.75 MB on N16R8). A factory reset deletes
the history too; the engineering menu deletes it on its own.

| `kind` | Content |
|---|---|
| `dose`, `prime`, `calib_run` | target and actual amount, result (`success`, `aborted`, `fault`, `locked`, `heartbeat_lost`, `max_duration`), trigger (`schedule`, `manual`, `api`, `boost`, `mqtt`) |
| `skip` | skipped dose with reason (`missed`, `daily_limit`, `delay_timeout`, …); after a power cut one entry with `count` |
| `container` | container set or refilled (`remaining_ml`, `capacity_ml`) |
| `calibration` | applied calibration (`ul_per_rev`, `measured_ml`, `target_ml`) |
| `tube` | tube counter set (`hours`) |
| `alarm` | start of an alarm: `container_low`, `tube_worn`, `rtc_fault` (pump 0 = device); driver faults and daily limit are already in as result or skipped dose |
| `alarm_end` | end of an alarm, same codes |
| `boost` | `started` (`total_ml`, `days` or `doses`), `cancelled` (by hand, `done_ml` so far), `finished` (by itself: all doses given or period over, `done_ml`) |
| `mode` | mode changed (`mode`: `auto`, `manual_only`, `disabled`) |
| `schedule` | schedule saved (`set` with `daily_ml` and `doses` per day) or deleted (`cleared`) |
| `settings` | other pump settings changed (name, speed, limits, thresholds, direction) |
| `locks` | lock rules changed (pump 0, `rules`) |
| `system` | device (pump 0): `boot` (`reason`: `power_on`, `restart`, `crash`, `watchdog`, `brownout`, …), `updated` (`version`, `from`), `restored` (backup), `time_set`, `cleared` (history deleted – the first entry after it), `settings` (language, time zone, LED, name), `network`, `mqtt`, `channels` (`pumps`), `engineering`, `password`, `key_added`, `key_removed` |

`source` says who it was: `manual` = web UI (signed-in browser), `api` = API key, `mqtt` = Home
Assistant/MQTT, `schedule` = schedule (also the end of a boost), `boost` = dose with boost share;
without `source` the device noticed it itself (alarms, start). A dose triggered by hand in the
web UI is therefore `manual`. What a backup changes on restore is only in the history as
`system`/`restored`. Start and firmware change are added as soon as the time is valid (RTC at
once, otherwise after the first time sync), with the time of the start.

The CSV has the columns `utc,local,kind,pump,source,code,target_ml,done_ml,value`; `value` is
the content (container), µl per revolution (calibration), hours (tube), the count (skipped),
days or negative number of doses (boost), the mode (mode), doses per day (schedule), the number
of rules (lock rules), reset reason or version (system).

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
| `<base>/state` | `{"critical","warning","conditions":["pump1_container_low","rtc_fault",…]}` |
| `<base>/pump/<n>/state` | `{"activity","total_ml","planned_ml","remaining_ml","days","next","schedule","amount_ml","tube_h","last"}` – on change, otherwise every 5 min |
| `<base>/event` | every history event on its own (QoS 1), fields as `GET /api/v1/history` plus `"replay"`; see below |
| `<base>/pump/<n>/dose/set` | `PRESS` = configured amount, or amount in ml (`2.5`) – only with control |
| `<base>/pump/<n>/amount/set` | amount for `PRESS` in ml (in RAM, start 1 ml) – only with control |
| `<base>/pump/<n>/schedule/set` | `ON` / `OFF` – mode automatic / manual only (a pump switched off stays off) – only with control |
| `<base>/stop/set` | anything – stop all pumps – only with control |
| `<base>/update` | `{"installed_version","latest_version","release_url"?,"in_progress","update_percentage"}` for the update entity; `latest_version` = installed as long as no newer one is known |
| `<base>/update/set` | `install` – install a newer version of the [update source](#ota-update) (never a free URL) – only with control |
| `<base>/ota/state` | progress as `GET /api/v1/ota/status` (at start, every 5 s, at the end) |

**Home Assistant discovery** (on by default): a device "LGAL Doser XXXX" by device discovery
(`homeassistant/device/lgal_doser_xxxx/config`) with per pump status, total dosed
(`total_increasing`, Home Assistant builds the daily values), planned today, stock, range in
days, next dose and tube operating hours (diagnostic); plus alarm and warning of the device and – with an update source – the update
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
| A · Mechanics | Dosing physics for all pumps: retract "default", calibration target, three speed tiers (rpm with ml/min and steps/s). Drivers per pump: run and holding current, microsteps, full steps/rev, acceleration, start speed, direction; optionally for all fitted pumps. Fitting: which pumps are built in (at least one); a pump not fitted is never addressed (no driver polling, no schedule, no alarm, dosing → 409 `pump_not_fitted`) and is hidden in the web UI and in Home Assistant; its settings are kept. Applies after a restart (saving restarts the device). |
| B · Limits | Priming max. duration and dead-man timeout, smallest single dose, daily limit window, schedule grid, max. delay of a scheduled dose. |
| C · Maintenance | Tube life, set/reset operating hours per pump. DS3231 temperature, aging offset: with a time server automatic trimming (measuring since, deviation now, last correction), without a time server input by hand, hint for a flat backup battery. History: recorded days and size, lost events, retention (1–60 days). |
| D · Diagnostics | Raw stepper test (steps forward/back, frequency, stop/immediate stop, live position and StallGuard; bypassing container, schedule and history). Driver diagnostics: status flags (short circuit, over-temperature, open coil, standstill, StealthChop, current scale) and TMC2209 raw registers. System telemetry incl. UART statistics and EEPROM self-test. Danger zone: delete history, reset network (the factory reset is on the maintenance page). |
| E · Firmware update | Update source (address of the release manifest, empty = none; saving checks at once) with module variant, allow unencrypted http (with warning); update from a URL with progress. |
| F · Backup | Back up and restore the whole device (see [Backup](#backup-m6)). |

The factory reset (maintenance page or button 10 s) stops all pumps, erases the EEPROM (run
state of dosing and schedule: counters, container, tube hours), NVS and the data partition and
restarts in setup mode.

## Button and LED

| Button (GPIO0) | Action on release | LED while holding |
|---|---|---|
| ≥ 3 s | setup Wi-Fi for 30 min (also to reset the admin password) | orange |
| ≥ 10 s | factory reset (stop pumps, erase EEPROM, NVS and data partition) | red |

Pressed at power-on = ESP download mode (hardware). A button held down permanently triggers
nothing as long as it has not been released once.

LED states, highest priority first:

| Colour | Meaning |
|---|---|
| red, fast blinking | alarm: a scheduled dose skipped today because of the daily limit, last run with driver fault, clock module not answering |
| magenta/white | firmware update running |
| violet pulsing | calibration or priming |
| cyan pulsing | pump running |
| orange pulsing | setup Wi-Fi on |
| yellow, slow blinking | warning: container almost empty (lasts fewer days than the alarm threshold, default 4), tube life reached, no network, no valid time |
| green / turquoise | ready, connected via LAN / Wi-Fi |
| white fading in | start |

Pumps switched off (mode "off") trigger neither alarm nor warning. The rule is in
`doser/alarms.h` (host-tested); `main.cpp` evaluates it every 2 s.

**Night mode** (settings → status LED or `PUT /api/v1/system/led`): dims the ready light
(green/turquoise) in a time window of the local time, e.g. 22:00–07:00, or switches it off.
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

All milestones are implemented and tested with host tests, fuzzers and the device mock. The test
on the finished hardware is still pending.

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
