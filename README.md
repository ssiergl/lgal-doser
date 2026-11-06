# LGAL-Doser – Firmware

Dosierpumpe „LG Aquatic Life“ (erstes Gerät: ESP32-S3-DEV-KIT-N16R8, 5× TMC2209, W5500).
Hardware-Beschreibung: [CLAUDE.md](CLAUDE.md).

Framework: **ESP-IDF v6.0.3**, C++ (gnu++2b), FreeRTOS.

## Struktur

```
components/lgal_core/   LG Aquatic Life Core: Netz, HTTP, OTA, LED, Taster, Einstellungen, Log
components/doser_hal/   Pinbelegung, sicherer Start; später TMC2209, Schritt-Takte, Füllstand
main/                   Verdrahtung der Komponenten (app_main)
tools/                  ap_password.py, ota_upload.py
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

## API (M0)

Alle Endpunkte mit HTTP-Basic-Auth (`admin` / Setup-Passwort). Das ist vorläufig, M4 bringt
Session-Login und API-Key.

| Methode | Pfad | Funktion |
|---|---|---|
| GET | `/` | Wartungsseite (im Firmware-Image) |
| GET | `/api/v1/system/info` | Version, Partition, OTA-Status, Netz, Speicher |
| GET | `/api/v1/system/log` | RAM-Log der letzten ~32 KB |
| POST | `/api/v1/system/reboot` | Neustart |
| GET / DELETE | `/api/v1/system/coredump` | letzter Absturz-Dump (ELF) / löschen |
| POST | `/api/v1/ota/firmware` | Firmware-Upload (rohe `.bin`) |

Coredump auswerten:

```bash
idf.py coredump-info -c coredump.elf
```

## Taster und LED

| Taster (GPIO0) | Aktion beim Loslassen | LED während des Haltens |
|---|---|---|
| ≥ 3 s | Setup-WLAN für 30 min | Orange |
| ≥ 10 s | Werkseinstellungen (NVS + Datenpartition löschen) | Rot |

Beim Einschalten gedrückt = ESP-Download-Modus (Hardware). Ein dauerhaft gedrückter
Taster löst nichts aus, solange er nicht einmal losgelassen wurde.

LED-Zustände in M0: Weiß aufblenden (Start), Orange pulsierend (Setup-WLAN),
Grün (LAN mit IP), Gelb blinkend (kein Netz), Magenta/Weiß (OTA).

## Meilensteine

- [x] **M0** Grundgerüst: Partitionen, Ethernet, Setup-WLAN, mDNS, OTA + Rollback, Wartungsseite, LED, Taster
- [ ] **M1** Hardware-Ebene: TMC2209-UART, Schritt-Takte (RMT + MCPWM/PCNT), RTC, Raw-Stepper-Test
- [ ] **M2** Dosier-Engine: Rampen, Rückzug, Kalibrierung, Priming, Behälter, Tageslimit
- [ ] **M3** Zeitplan, Zeitumstellung, Kollisionsschutz, Boost, Verhalten nach Stromausfall (mit Host-Tests)
- [ ] **M4** WLAN-Client, Captive Portal, Ersteinrichtung, Auth, REST, WebSocket
- [ ] **M5** WebUI (SPA, eingebettet ins Firmware-Image)
- [ ] **M6** MQTT + Home-Assistant-Discovery, Historie, Backup, Engineering-Modus
