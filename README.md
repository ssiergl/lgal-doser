# LGAL-Doser – Firmware

Dosierpumpe „LG Aquatic Life“ (erstes Gerät: ESP32-S3-DEV-KIT-N16R8, 5× TMC2209, W5500).
Hardware-Beschreibung: [CLAUDE.md](CLAUDE.md).

Framework: **ESP-IDF v6.0.3**, C++ (gnu++2b), FreeRTOS.

## Struktur

```
components/lgal_core/   LG Aquatic Life Core: Netz, HTTP, OTA, LED, Taster, Einstellungen, Log
components/doser_hal/   Pinbelegung, sicherer Start, TMC2209-UART, Schritt-Takte, Pumpen, RTC/EEPROM
components/doser_app/   Geräte-Logik: Hardwaretest, Dosier-Engine, Zeitplan (Scheduler) und ihre REST-APIs
main/                   Verdrahtung der Komponenten (app_main)
tools/                  ap_password.py, ota_upload.py
tests/host/             Host-Tests (TMC-Protokoll, Strom, Rampe, Kalender, Zeitzonen, JSON, Dosier- und Zeitplan-Logik)
                        und Simulation von Dosier-Engine und Scheduler (`tests/host/run.sh`)
partitions.csv          endgültige Partitionstabelle (nur per USB änderbar!)
```

## Toolchain

```bash
git clone -b v6.0.3 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v6.0.3
```

```bash
~/esp/esp-idf-v6.0.3/install.sh esp32s3 && brew install cmake ninja
```

In jeder neuen Shell:

```bash
source ~/esp/esp-idf-v6.0.3/export.sh
```

Bauen:

```bash
idf.py build
```

Host-Tests (ohne ESP-IDF, laufen auf dem Mac):

```bash
tests/host/run.sh
```

Nach Änderungen an `sdkconfig.defaults` muss `sdkconfig` neu erzeugt werden
(`rm sdkconfig && idf.py build`). Die IRAM-Optionen der Schritt-Takte werden
beim Bauen geprüft.

## Erstinbetriebnahme (einmalig per USB, vor dem Einbau)

Bootloader, Rollback-Einstellung und Partitionstabelle werden **nur** beim USB-Flash
geschrieben. Danach laufen alle Updates per OTA.

1. **Geräte-Geheimnis festlegen** (einmalig, danach nie wieder ändern – sonst
   ändern sich alle Setup-Passwörter). Datei `sdkconfig.secrets` anlegen
   (liegt nicht in Git):

   ```
   CONFIG_LGAL_AP_SECRET="hier-ein-langes-zufaelliges-geheimnis"
   ```

   Danach `sdkconfig` löschen, damit die Werte neu eingelesen werden
   (ESP-IDF übernimmt Defaults nur in ein neues `sdkconfig`):

   ```bash
   rm -f sdkconfig && idf.py build
   ```

2. **Flashen per USB** (Board noch nicht eingesteckt):

   ```bash
   idf.py -p /dev/cu.usbmodem* flash monitor
   ```

   Die serielle Ausgabe zeigt MAC, Hostname, Setup-WLAN und Passwort.
   **MAC und Passwort notieren** (z. B. Aufkleber ins Gehäuse). Das Passwort lässt
   sich jederzeit aus der MAC neu berechnen:

   ```bash
   python3 tools/ap_password.py AA:BB:CC:DD:EE:FF
   ```

   Den Engineering-Schlüssel (nur Hersteller, nicht aufs Etikett) liefert
   `python3 tools/ap_password.py AA:BB:CC:DD:EE:FF --engineering`.

3. Board einstecken, LAN-Kabel anschließen.

## Zugang

| Weg | Adresse | Anmeldung |
|---|---|---|
| LAN | `http://lgal-doser-xxxx.local/` oder DHCP-IP | `/login` |
| Setup-WLAN `LG-Aquatic-Life-Setup-XXXX` | `http://192.168.4.1/` | WLAN: Geräte-Passwort, Web: `/login` |

Anmeldung auf `/login` (Seite „Account“):
- **Erste Anmeldung** mit dem Geräte-Passwort vom Etikett. Danach verlangt die Seite ein
  eigenes Admin-Passwort (8–64 Zeichen); ab dann gilt nur noch dieses.
- **Sitzungen** liegen nur im RAM (Cookie `lgal_session`, max. 8, 7 Tage ohne Nutzung bzw.
  30 Tage absolut). Nach Neustart oder OTA neu anmelden. Passwort ändern meldet alle
  anderen Browser ab.
- **API-Keys** (max. 4) für App, Home Assistant und Skripte, Header `X-API-Key: lgal_…` oder
  `Authorization: Bearer lgal_…`. Der Key wird nur einmal angezeigt; gespeichert ist nur
  sein SHA-256. Keys überleben Neustarts und Passwortwechsel.
- **Fehlversuche:** 5 frei, danach Wartezeit pro IP ab 1 s, verdoppelt bis 300 s. Eine IP wird
  erst nach 1 h Ruhe vergessen; sind alle 8 Plätze belegt, teilen sich weitere IPs eine
  gemeinsame Wartezeit (viele Adressen bremsen sich gegenseitig aus).
- **Passwort vergessen:** Taster 3 s halten → Setup-WLAN. Darüber verbunden kann `/login`
  mit dem Geräte-Passwort ein neues Admin-Passwort setzen. Einstellungen, Zeitpläne und
  Kalibrierung bleiben erhalten.
- **Engineering** (Super-Admin, nur Hersteller): `/login?eng=1`, Schlüssel pro Gerät aus
  dem Geheimnis abgeleitet. Gilt für die angemeldete Sitzung und sperrt 15 min nach der
  letzten Engineering-Anfrage. Mit API-Key zusätzlich Header `X-Engineering-Key`.

Browser-Seiten ohne Anmeldung leiten auf `/login?next=…` um, API-Aufrufe antworten
mit 401 `unauthorized` bzw. 403 `engineering_locked`.

Im Werkszustand (`setup_completed = false`) ist das Setup-WLAN immer an. Die
Ersteinrichtung, die es abschaltet, folgt in M4.

## OTA-Update

Über die Wartungsseite (`/`) per Datei-Upload oder per Skript:

```bash
python3 tools/ota_upload.py lgal-doser-xxxx.local --key lgal_...
```

Anmeldung: `--key` (oder `LGAL_API_KEY`), `--password` (oder `LGAL_PASSWORD`, Admin-Passwort)
oder vor der Ersteinrichtung `--mac AA:BB:CC:DD:EE:FF` (Geräte-Passwort). Mit Passwort meldet
sich das Skript nach dem Neustart selbst wieder an.

Ablauf und Schutz:
- Das Image wird vor dem Schreiben geprüft (ESP32-S3, Projektname `lgal_doser`) und
  nach dem Schreiben per Hash verifiziert.
- Das neue Image startet als `PENDING_VERIFY` und wird erst nach 60 s stabiler
  Laufzeit mit erreichbarem Webserver als gültig markiert.
- Absturz, Watchdog oder fehlgeschlagener Health-Check vorher: automatischer
  **Rollback** auf die vorherige Firmware.
- Die Pumpentreiber werden für die Dauer des Updates abgeschaltet.

## API

Alle Endpunkte brauchen eine Sitzung oder einen API-Key (siehe [Zugang](#zugang)). Ohne
Anmeldung erreichbar: `/login`, `/api/v1/auth/status`, `/api/v1/auth/login`,
`/api/v1/auth/recover` und `GET /api/v1/system/language`. Die `engineering`-Endpunkte und
alle `/api/v1/hw/…`-Endpunkte brauchen zusätzlich die Engineering-Freischaltung.

**Mehrsprachigkeit:** Firmware, Logs und API sind englisch. Fehler kommen immer als
`{"error":{"code":"pump_busy","message":"Pump is busy"}}`; der `code` ist fest und wird von den
Seiten übersetzt (Englisch Standard, Deutsch; weitere Sprachen: Eintrag in
`settings::kLanguages` + Wörterbücher in `i18n.js` und den Seiten). Die Sprache der Oberfläche
ist eine Geräteeinstellung und lässt sich auf jeder Seite umschalten.

| Methode | Pfad | Funktion |
|---|---|---|
| GET | `/` | Wartungsseite (im Firmware-Image) |
| GET | `/login` | Anmeldung, Passwort, API-Keys, Engineering |
| GET | `/api/v1/auth/status` | angemeldet?, Passwort gesetzt?, Engineering-Restzeit, Wiederherstellung möglich? |
| POST | `/api/v1/auth/login` | `{"password"}` → Sitzungs-Cookie |
| POST | `/api/v1/auth/logout` | Sitzung beenden |
| POST | `/api/v1/auth/password` | `{"current","new"}` (vor dem ersten Setzen ohne `current`) |
| POST | `/api/v1/auth/recover` | `{"device_password","new"}`, nur aus dem Setup-WLAN |
| GET / POST / DELETE | `/api/v1/auth/keys[?slot=1]` | API-Keys auflisten / `{"name"}` anlegen / löschen |
| POST / DELETE | `/api/v1/auth/engineering` | `{"key"}` freischalten / sperren |
| GET | `/api/v1/system/info` | Version, Partition, OTA-Status, Netz, Speicher, Zeit, Sprache, Links |
| GET / POST | `/api/v1/system/language?lang=en\|de` | Sprache der Oberfläche |
| GET | `/i18n.js` | gemeinsame Übersetzungsbasis der Seiten (ohne Anmeldung) |
| GET | `/api/v1/system/log` | RAM-Log der letzten ~32 KB |
| POST | `/api/v1/system/reboot` | Neustart |
| GET / DELETE | `/api/v1/system/coredump` | letzter Absturz-Dump (ELF) / löschen |
| POST | `/api/v1/ota/firmware` | Firmware-Upload (rohe `.bin`), abgelehnt solange eine Pumpe läuft |
| GET | `/dosing` | Dosiertest-Seite |
| GET | `/api/v1/pumps` | Status, Kalibrierung, Rückzug, Limit, Behälter, Schlauch aller Pumpen |
| POST | `/api/v1/pumps/dose?pump=1&ml=1.5` | Dosieren (optional `tier=0..2`). Während einer Sperrzeit: 409 `collision_lock` mit `retry_at`/`retry_local`, oder 202 `queued` (Sperrzeit-Regel „vormerken“) |
| POST | `/api/v1/pumps/stop?pump=0..5` | sanfter Stopp, Rückzug läuft noch (0 = alle); verwirft eine vorgemerkte Dosis |
| POST | `/api/v1/pumps/prime?pump=1&action=start\|beat\|stop` | Entlüften mit Totmann-Heartbeat |
| POST | `/api/v1/pumps/calibrate?pump=1&action=start[&ml=20]` | Kalibrierlauf; danach `action=apply&measured_ml=…` oder `cancel` |
| POST | `/api/v1/pumps/settings?pump=1&…` | `name`, `mode=auto\|manual_only\|disabled`, `tier`, `daily_limit_ml`, `retract_rev` (oder `default`) |
| POST | `/api/v1/pumps/container?pump=1&…` | `capacity_ml`, `remaining_ml` (nur Kapazität = neuer, voller Behälter) |
| POST | `/api/v1/pumps/tube?pump=1&hours=0` | Schlauchzähler setzen/zurücksetzen |
| GET / POST | `/api/v1/engineering/dosing?…` | Super-Admin: `retract_rev`, `calib_ml`, `tier1_rpm`…`tier3_rpm`, `prime_max_s`, `prime_heartbeat_ms`, `min_dose_ml`, `tube_life_h`, `limit_mode=calendar_day\|rolling_24h` |
| GET | `/schedule` | Zeitplantest-Seite |
| GET / PUT | `/api/v1/pumps/schedule?pump=1` | Zeitplan einer Pumpe (JSON, siehe unten) + heutige Dosen |
| POST | `/api/v1/pumps/boost?pump=1&ml=30&days=3` | Boost starten; `action=stop` beendet ihn |
| GET | `/api/v1/schedule/status` | nächste Dosis, Verzögerung, Warteschlange, Übersprungene, Boost, Reichweite aller Pumpen |
| GET / PUT | `/api/v1/schedule/locks` | Sperrzeiten zwischen Pumpen + Regel für manuelle Dosen (JSON) |
| GET / POST | `/api/v1/engineering/scheduler?…` | Super-Admin: `grid_min` (Teiler von 1440, ≤ 60), `max_delay_min` |
| GET | `/hwtest` | Hardwaretest-Seite |
| GET | `/api/v1/hw/status` | Treiber, Pumpen, RTC, EEPROM, UART-Statistik |
| POST | `/api/v1/hw/pump/run?pump=1&steps=±N&sps=N` | Raw-Fahrt (optional `accel`, `vstart`, `force=1`) |
| POST | `/api/v1/hw/pump/stop?pump=0..5&mode=soft\|hard` | Stopp (0 = alle) |
| POST | `/api/v1/hw/pump/config?pump=1&run_ma=…` | `run_ma`, `hold_ma`, `microsteps`, `full_steps`, `invert`, `accel`, `vstart` |
| GET | `/api/v1/hw/tmc?pump=1` | TMC2209-Rohregister |
| POST | `/api/v1/hw/time?utc=…` | Zeit manuell setzen (System + RTC) |
| POST | `/api/v1/hw/rtc/aging?offset=…` | DS3231 Aging-Offset |
| POST | `/api/v1/hw/eeprom/test` | EEPROM-Schreib-/Lesetest |

Coredump auswerten:

```bash
idf.py coredump-info -c coredump.elf
```

## Hardware (M1)

| Baugruppe | Umsetzung |
|---|---|
| TMC2209 | Eindraht-UART auf GPIO38 (Pumpe 1–4) / GPIO39 (Pumpe 5), 115200 Bd. Echo wird geprüft, für die Antwort wird der Pin losgelassen (SENDDELAY = 24 Bit). Konfiguration: StealthChop, Interpolation, Strom per Register (Rsense 0,11 Ω), Freilauf bei Haltestrom 0. Kontrolle über IFCNT, Neukonfiguration nach Treiber-Reset (GSTAT). |
| Schritt-Takte | Pumpe 1–4: RMT (exakte Schrittzahl per Encoder). Pumpe 5: MCPWM + PCNT (Zähler stoppt beim Zielwert). Trapezrampe in Ganzzahl-Arithmetik, alle 5 parallel, IRAM-sicher. |
| EN | gemeinsam: an, sobald eine Pumpe fährt; 0,5 s nach der letzten Fahrt aus (außer Haltestrom > 0). |
| Überwachung | alle 100 ms während der Fahrt: SG_RESULT, DRV_STATUS (Kurzschluss/Übertemperatur → Stopp). Reihum jede Sekunde: Treiber vorhanden, GSTAT. |
| Uhr | DS3231 speichert UTC. Start: Zeit aus RTC. NTP stellt die RTC nach. Ohne NTP stündlicher Abgleich aus der RTC. Zeitzone POSIX (`CET-1CEST,M3.5.0,M10.5.0/3`). |
| EEPROM | AT24C32 mit Seitenschreiben + ACK-Polling. Ab M2 Laufzustand der Dosierung (siehe unten). |

Standardwerte pro Pumpe (über `/hwtest` änderbar, in NVS gespeichert): 600 mA Laufstrom,
0 mA Haltestrom, 16 Mikroschritte, 200 Vollschritte/U (bitte am Motor prüfen),
Rampe 600 U/min pro s, Start 20 U/min, max. 600 U/min.

Die Testseite **`/hwtest`** (Link auf der Wartungsseite) zeigt Treiber, StallGuard,
RTC und EEPROM und fährt einzelne Pumpen um X Umdrehungen bzw. Schritte.
„Ohne Treiber takten“ erzeugt STEP-Pulse auch ohne antwortenden TMC2209 (Test mit
Oszilloskop/Logikanalysator).

## Dosierung (M2)

| Thema | Umsetzung |
|---|---|
| Menge → Schritte | Kalibrierung in µl pro Motorumdrehung (unabhängig von den Mikroschritten). Ohne Kalibrierung Nennwert 0,37 ml/U (≈ 111 ml/min bei 300 U/min). |
| Ablauf | Rückzug ausgleichen → Hauptfahrt → Anti-Tropf-Rückzug. Der ausgeführte Rückzug wird spannungsausfallsicher gespeichert. |
| Verbuchung | Beim Start wird die volle Menge auf Tageslimit, Behälter und Gesamtzähler gebucht und gesichert; ein Abbruch schreibt die nicht geförderte Menge gut. Stromausfall während der Fahrt → Menge bleibt gebucht (sichere Seite). |
| Tageslimit | hart pro Pumpe; Fenster Kalendertag oder rollierend 24 h (Engineering). Stunden-Töpfe über 26 h, angebrochene Stunden zählen voll. Ohne gültige Uhrzeit werden Pumpen mit Limit gesperrt. |
| Kalibrierung | Lauf mit Zielmenge (Standard 20 ml), dann gemessene Menge eintragen. Plausibel: 0,25- bis 4-facher Nennwert. Die aufgefangene Menge wird vom Behälter abgezogen. |
| Entlüften | läuft, solange der Button gehalten wird (Heartbeat alle 150 ms, Stopp nach 500 ms ohne Heartbeat, max. 60 s). Zählt nicht für Behälter/Limit, aber für die Schlauch-Laufzeit. |
| Sperren | Betriebsart `disabled`, OTA-Update, Treiberfehler. |
| Speicher | Einstellungen in NVS (`dose`). Laufzustand je Pumpe im EEPROM als A/B-Datensätze (Folgenummer + CRC32, ab 0x000), Rückfall auf NVS ohne EEPROM. |

Engineering-Standardwerte (Super-Admin, global; Rückzug zusätzlich pro Pumpe):
Rückzug ¼ U (≈ 0,09 ml), Kalibrier-Zielmenge 20 ml, Stufen 100/200/300 U/min
(≈ 37/74/111 ml/min), Entlüften max. 60 s / Heartbeat 500 ms, kleinste Dosis 0,02 ml,
Schlauch-Lebensdauer 1000 h, Tageslimit pro Kalendertag. Änderungen brauchen die
Engineering-Freischaltung (siehe [Zugang](#zugang)).

Die Testseite **`/dosing`** bedient alles davon pro Pumpe; unten liegen die
Engineering-Parameter.

## Zeitplan (M3)

| Thema | Umsetzung |
|---|---|
| Programme | bis 8 pro Pumpe: Einzelzeitpunkt oder Generator („X ml in N Dosen zwischen A und B“, gleichmäßig auf das Raster gelegt, Rundungsrest auf die ersten Dosen). Höchstens 96 Dosen pro Tag und Pumpe. |
| Tage | täglich, Wochentage oder „alle N Tage“ (1–30) ab einem Startdatum. |
| Raster | Zeiten auf dem Zeitplan-Raster (Standard 5 min, Engineering). Ortszeit der Geräte-Zeitzone. |
| Zeitumstellung | Zeitzone als POSIX-Regel. Fällt eine Zeit in die Lücke im Frühjahr, wird sie nach hinten verschoben; eine doppelte Stunde im Herbst wird einmal (beim ersten Auftreten) dosiert. |
| Prüfung beim Speichern | alle Pumpen zusammen: Laufzeit jeder Dosis (Menge, Kalibrierung, Geschwindigkeit, Rampe, Rückzug + 2 s) darf die nächste Dosis derselben Pumpe nicht erreichen; Sperrzeiten zwischen Pumpen auch über Mitternacht und für Intervall-Tage; Tageslimit. Verstöße → 409 mit Liste der kollidierenden Dosen, nichts wird gespeichert. |
| Laufzeit-Schutz | Jede Dosierung (auch manuell und Boost) prüft die Sperrzeiten gegen das tatsächliche Ende der letzten Dosis der anderen Pumpe. Eine geplante Dosis wartet dann und wird nach `max_delay_min` (Standard 30 min) verworfen. Entlüften und Kalibrieren sind ausgenommen. |
| Manuelle Dosis in Sperrzeit | Regel `reject` (Standard): 409 mit Zeitpunkt, ab dem es geht. Regel `queue`: vorgemerkt (eine pro Pumpe), startet nach Ende der Sperrzeit. |
| Stromausfall | Bis 2 min Verspätung gilt eine Dosis als pünktlich. Danach wird sie übersprungen (Standard) oder innerhalb eines einstellbaren Fensters nachgeholt. Übersprungene Dosen werden gezählt und mit Grund angezeigt; nach langen Ausfällen wird alles Ältere in einem Schritt übersprungen. |
| Boost | Zusatzmenge über 1–30 Tage, gleichmäßig auf die geplanten Dosen des Tages verteilt; was nicht dosiert werden kann, wandert auf die Folgetage. Endet von selbst. |
| Reichweite | Behälter-Restmenge geteilt durch den Durchschnitt der nächsten 7 Tage (inkl. Boost). |
| Ohne gültige Uhrzeit | Es wird nichts geplant; manuelle Dosen bleiben möglich (außer bei Pumpen mit Tageslimit). |
| Speicher | Programme, Sperrzeiten, Parameter in NVS (`sched`). Fortschritt je Pumpe (zuletzt bearbeitete Dosis, Boost, Zähler) im EEPROM als A/B-Datensätze ab 0x640, Rückfall auf NVS. Geschrieben wird nur, wenn eine Dosis bearbeitet wurde. |

Zeitplan als JSON (`PUT /api/v1/pumps/schedule?pump=1`; unbekannte Felder werden ignoriert, ein
GET-Ergebnis kann also bearbeitet zurückgeschickt werden):

```json
{"power_loss": "skip_missed", "catch_up_min": 60,
 "programs": [
  {"kind": "generator", "start": "08:00", "end": "20:00", "count": 12, "ml": 24,
   "days": {"mode": "daily"}},
  {"kind": "single", "time": "21:30", "ml": 2.5, "enabled": true,
   "days": {"mode": "weekdays", "weekdays": ["mon", "fri"]}},
  {"kind": "single", "time": "09:00", "ml": 5,
   "days": {"mode": "interval", "every": 2, "anchor": "2026-11-06"}}]}
```

Sperrzeiten (`PUT /api/v1/schedule/locks`):

```json
{"manual": "reject", "rules": [{"pumps": [1, 2], "gap_min": 5}]}
```

Fehler nennen die Stelle (`"program": 2, "field": "time"`) bzw. bei 409 `conflict` die
kollidierenden Dosen (`"conflicts": [{"code": "collision_lock", "first": {…}, "second": {…}, "gap_min": 5}]`).
Zeitpunkte kommen als UTC-Sekunden plus Ortszeit des Geräts (`"at"` + `"local"`), weil der
Browser in einer anderen Zeitzone sein kann.

Die Testseite **`/schedule`** zeigt den Status aller Pumpen (inkl. Boost und manueller Dosis)
und bearbeitet Zeitpläne, Sperrzeiten und die Scheduler-Parameter.

## Taster und LED

| Taster (GPIO0) | Aktion beim Loslassen | LED während des Haltens |
|---|---|---|
| ≥ 3 s | Setup-WLAN für 30 min (auch zum Zurücksetzen des Admin-Passworts) | Orange |
| ≥ 10 s | Werkseinstellungen (NVS + Datenpartition löschen) | Rot |

Beim Einschalten gedrückt = ESP-Download-Modus (Hardware). Ein dauerhaft gedrückter
Taster löst nichts aus, solange er nicht einmal losgelassen wurde.

LED-Zustände: Weiß aufblenden (Start), Orange pulsierend (Setup-WLAN),
Grün (LAN mit IP), Gelb blinkend (kein Netz), Magenta/Weiß (OTA), Cyan (Pumpe läuft),
Violett pulsierend (Kalibrierung/Entlüften).

## Meilensteine

- [x] **M0** Grundgerüst: Partitionen, Ethernet, Setup-WLAN, mDNS, OTA + Rollback, Wartungsseite, LED, Taster
- [x] **M1** Hardware-Ebene: TMC2209-UART, Schritt-Takte (RMT + MCPWM/PCNT), RTC, Raw-Stepper-Test
- [x] **M2** Dosier-Engine: Rückzug, Kalibrierung, Priming, Behälter, Tageslimit, Schlauch-Laufzeit; Mehrsprachigkeit (en/de)
- [x] **M3** Zeitplan, Zeitumstellung, Kollisionsschutz, Boost, Verhalten nach Stromausfall (mit Host-Tests)
- [ ] **M4** WLAN-Client, Captive Portal, Ersteinrichtung, Auth, REST, WebSocket
- [ ] **M5** WebUI (SPA, eingebettet ins Firmware-Image)
- [ ] **M6** MQTT + Home-Assistant-Discovery, Historie, Backup, Engineering-Modus
