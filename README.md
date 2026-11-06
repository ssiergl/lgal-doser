# LGAL-Doser – Firmware

Dosierpumpe „LG Aquatic Life“ (erstes Gerät: ESP32-S3-DEV-KIT-N16R8, 5× TMC2209, W5500).
Hardware-Beschreibung: [CLAUDE.md](CLAUDE.md).

Framework: **ESP-IDF v6.0.3**, C++ (gnu++2b), FreeRTOS.

## Struktur

```
components/lgal_core/   LG Aquatic Life Core: Netz, HTTP, OTA, LED, Taster, Einstellungen, Log
components/doser_hal/   Pinbelegung, sicherer Start, TMC2209-UART, Schritt-Takte, Pumpen, RTC/EEPROM
components/doser_app/   Geräte-Logik: gespeicherte Hardware-Parameter, Hardwaretest (später Dosier-Engine)
main/                   Verdrahtung der Komponenten (app_main)
tools/                  ap_password.py, ota_upload.py
tests/host/             Host-Tests (TMC-Protokoll, Strom, Rampe, Kalender, Dosier-Logik) und Simulation der Dosier-Engine
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

3. Board einstecken, LAN-Kabel anschließen.

## Zugang

| Weg | Adresse | Anmeldung |
|---|---|---|
| LAN | `http://lgal-doser-xxxx.local/` oder DHCP-IP | `admin` / Setup-Passwort |
| Setup-WLAN `LG-Aquatic-Life-Setup-XXXX` | `http://192.168.4.1/` | WLAN + Web: Setup-Passwort |

Im Werkszustand (`setup_completed = false`) ist das Setup-WLAN immer an. Die
Ersteinrichtung, die es abschaltet, folgt in M4.

## OTA-Update

Über die Wartungsseite (`/`) per Datei-Upload oder per Skript:

```bash
python3 tools/ota_upload.py lgal-doser-xxxx.local --mac AA:BB:CC:DD:EE:FF
```

Ablauf und Schutz:
- Das Image wird vor dem Schreiben geprüft (ESP32-S3, Projektname `lgal_doser`) und
  nach dem Schreiben per Hash verifiziert.
- Das neue Image startet als `PENDING_VERIFY` und wird erst nach 60 s stabiler
  Laufzeit mit erreichbarem Webserver als gültig markiert.
- Absturz, Watchdog oder fehlgeschlagener Health-Check vorher: automatischer
  **Rollback** auf die vorherige Firmware.
- Die Pumpentreiber werden für die Dauer des Updates abgeschaltet.

## API

Alle Endpunkte mit HTTP-Basic-Auth (`admin` / Setup-Passwort). Das ist vorläufig, M4 bringt
Session-Login und API-Key.

**Mehrsprachigkeit:** Firmware, Logs und API sind englisch. Fehler kommen immer als
`{"error":{"code":"pump_busy","message":"Pump is busy"}}`; der `code` ist fest und wird von den
Seiten übersetzt (Englisch Standard, Deutsch; weitere Sprachen: Eintrag in
`settings::kLanguages` + Wörterbücher in `i18n.js` und den Seiten). Die Sprache der Oberfläche
ist eine Geräteeinstellung und lässt sich auf jeder Seite umschalten.

| Methode | Pfad | Funktion |
|---|---|---|
| GET | `/` | Wartungsseite (im Firmware-Image) |
| GET | `/api/v1/system/info` | Version, Partition, OTA-Status, Netz, Speicher, Zeit, Sprache, Links |
| GET / POST | `/api/v1/system/language?lang=en\|de` | Sprache der Oberfläche |
| GET | `/i18n.js` | gemeinsame Übersetzungsbasis der Seiten (ohne Anmeldung) |
| GET | `/api/v1/system/log` | RAM-Log der letzten ~32 KB |
| POST | `/api/v1/system/reboot` | Neustart |
| GET / DELETE | `/api/v1/system/coredump` | letzter Absturz-Dump (ELF) / löschen |
| POST | `/api/v1/ota/firmware` | Firmware-Upload (rohe `.bin`), abgelehnt solange eine Pumpe läuft |
| GET | `/dosing` | Dosiertest-Seite |
| GET | `/api/v1/pumps` | Status, Kalibrierung, Rückzug, Limit, Behälter, Schlauch aller Pumpen |
| POST | `/api/v1/pumps/dose?pump=1&ml=1.5` | Dosieren (optional `tier=0..2`) |
| POST | `/api/v1/pumps/stop?pump=0..5` | sanfter Stopp, Rückzug läuft noch (0 = alle) |
| POST | `/api/v1/pumps/prime?pump=1&action=start\|beat\|stop` | Entlüften mit Totmann-Heartbeat |
| POST | `/api/v1/pumps/calibrate?pump=1&action=start[&ml=20]` | Kalibrierlauf; danach `action=apply&measured_ml=…` oder `cancel` |
| POST | `/api/v1/pumps/settings?pump=1&…` | `name`, `mode=auto\|manual_only\|disabled`, `tier`, `daily_limit_ml`, `retract_rev` (oder `default`) |
| POST | `/api/v1/pumps/container?pump=1&…` | `capacity_ml`, `remaining_ml` (nur Kapazität = neuer, voller Behälter) |
| POST | `/api/v1/pumps/tube?pump=1&hours=0` | Schlauchzähler setzen/zurücksetzen |
| GET / POST | `/api/v1/engineering/dosing?…` | Super-Admin: `retract_rev`, `calib_ml`, `tier1_rpm`…`tier3_rpm`, `prime_max_s`, `prime_heartbeat_ms`, `min_dose_ml`, `tube_life_h`, `limit_mode=calendar_day\|rolling_24h` |
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
Schlauch-Lebensdauer 1000 h, Tageslimit pro Kalendertag. Der eigene Engineering-Schlüssel
folgt mit der Anmeldung in M4.

Die Testseite **`/dosing`** bedient alles davon pro Pumpe; unten liegen die
Engineering-Parameter.

## Taster und LED

| Taster (GPIO0) | Aktion beim Loslassen | LED während des Haltens |
|---|---|---|
| ≥ 3 s | Setup-WLAN für 30 min | Orange |
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
- [ ] **M3** Zeitplan, Zeitumstellung, Kollisionsschutz, Boost, Verhalten nach Stromausfall (mit Host-Tests)
- [ ] **M4** WLAN-Client, Captive Portal, Ersteinrichtung, Auth, REST, WebSocket
- [ ] **M5** WebUI (SPA, eingebettet ins Firmware-Image)
- [ ] **M6** MQTT + Home-Assistant-Discovery, Historie, Backup, Engineering-Modus
