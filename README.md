# LGAL-Doser – Firmware

Dosierpumpe „LG Aquatic Life“ (erstes Gerät: ESP32-S3-DEV-KIT-N16R8, 5× TMC2209, W5500).
Hardware-Beschreibung: [CLAUDE.md](CLAUDE.md).

Framework: **ESP-IDF v6.0.3**, C++ (gnu++2b), FreeRTOS.

## Struktur

```
components/lgal_core/   LG Aquatic Life Core: Netz, HTTP, OTA, LED, Taster, Einstellungen, Log
components/doser_hal/   Pinbelegung, sicherer Start, TMC2209-UART, Schritt-Takte, Pumpen, RTC/EEPROM
components/doser_app/   Geräte-Logik: Hardwaretest, Dosier-Engine, Zeitplan (Scheduler) und ihre REST-APIs
components/webui/       Weboberfläche (M5): www/ (Preact + htm, ohne Build), pack.py bettet sie gzip-komprimiert ein
main/                   Verdrahtung der Komponenten (app_main)
tools/                  ap_password.py, ota_upload.py
tests/host/             Host-Tests (TMC-Protokoll, Strom, Rampe, Kalender, Zeitzonen, JSON, Dosier- und Zeitplan-Logik, LED-Alarme, Historie, Datensicherung)
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
| LAN | `http://lgal-doser-xxxx.local/` oder DHCP-/feste IP | `/login` |
| WLAN (Ersatz oder fest, siehe [Netzwerk](#netzwerk-m4)) | `http://lgal-doser-xxxx.local/` oder IP | `/login` |
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

Im Werkszustand (`setup_completed = false`) ist das Setup-WLAN immer an und `/` führt
zur [Ersteinrichtung](#ersteinrichtung-m4).

## Ersteinrichtung (M4)

1. Mit dem Setup-WLAN `LG-Aquatic-Life-Setup-XXXX` verbinden (Passwort vom Etikett).
   Das **Captive Portal** öffnet die Einrichtung auf Handy und Laptop meist von selbst;
   sonst `http://192.168.4.1/` aufrufen.
2. Mit dem Geräte-Passwort anmelden; die Seite **`/setup`** führt durch fünf Schritte:
   Sprache → eigenes Admin-Passwort → Netzwerk (LAN-Status, WLAN suchen, testen,
   speichern) → Zeitzone, NTP-Server, ggf. Uhrzeit vom Browser → Abschluss.
3. **Abschließen** geht erst mit Admin-Passwort und verbundenem LAN oder WLAN. Die Seite
   zeigt die neuen Adressen (`http://lgal-doser-xxxx.local/` und IP); 20 s später schaltet
   das Setup-WLAN ab.

| Thema | Verhalten |
|---|---|
| Captive Portal | DNS-Server auf dem Setup-WLAN beantwortet jeden Namen (A) mit `192.168.4.1`, andere Typen leer. Unbekannte Seiten leiten Setup-WLAN-Clients auf `http://192.168.4.1/` um. LAN/WLAN-Clients bekommen ein normales 404. |
| Wiederholen | `/setup` bleibt nach dem Abschluss erreichbar (Link auf der Wartungsseite), schaltet das Setup-WLAN dann aber nicht erneut ein. |
| Werkseinstellungen | Taster 10 s setzt `setup_completed` zurück → Ersteinrichtung beginnt von vorn. |

## Netzwerk (M4)

Seite **`/network`**: Status, Einstellungen, WLAN-Suche und Verbindungstest.

| Thema | Verhalten |
|---|---|
| Schnittstelle | `auto` (Standard): LAN hat Vorrang. Ist das LAN 30 s ohne Adresse (Kabel weg, kein DHCP), startet der WLAN-Client; sobald das LAN wieder eine Adresse hat, geht das WLAN-Funkmodul aus. `lan`: nur LAN, WLAN bleibt aus. `wlan`: nur WLAN, Ethernet wird abgeschaltet. |
| Adressen | LAN und WLAN getrennt: DHCP oder fest (IP, Maske /8–/30, Gateway, DNS 1/2). Die DNS-Server folgen der aktiven Schnittstelle. |
| WLAN | WPA2 oder neuer (Passwort 8–63 Zeichen) oder offen. Wiederverbindung mit Wartezeit 2 s, verdoppelt bis 60 s. |
| Übernahme | Gespeichert sofort (NVS `net`), angewendet 1 s nach der Antwort – sonst erreicht die Antwort bei einer neuen IP den Browser nicht mehr. |
| Verbindungstest | Prüft SSID/Passwort/Adressen ohne zu speichern (max. 25 s); danach gelten wieder die gespeicherten Einstellungen. |
| Ausgesperrt? | Taster 3 s → Setup-WLAN, dort `/network` öffnen. Das Setup-WLAN läuft parallel zum WLAN-Client. |

Einstellungen (`PUT /api/v1/net/settings`; fehlende Felder bleiben, leere Adresse = keine):

```json
{"mode": "auto",
 "lan":  {"dhcp": false, "ip": "192.168.1.50", "netmask": "255.255.255.0",
          "gateway": "192.168.1.1", "dns1": "192.168.1.1", "dns2": ""},
 "wlan": {"ssid": "Riff", "password": "…", "dhcp": true}}
```

Das WLAN-Passwort wird nie zurückgegeben (`"password_set": true`); ohne `password` bleibt das
gespeicherte. Fehler: 400 `invalid_param` mit `field` (z. B. `lan.gateway`), 400
`wlan_not_configured` für `wlan` ohne SSID.

## WebSocket (M4)

**`/api/v1/ws`** für Echtzeit-Werte und das Entlüften. Anmeldung wie bei der REST-API
(Sitzungs-Cookie oder API-Key-Header). Nachrichten sind JSON-Textframes.

| Thema | Verhalten |
|---|---|
| Status-Push | `{"type":"status","seq","utc","time_valid","pumps":[…]}` direkt nach dem Verbinden, bei jeder Änderung (geprüft alle 250 ms) und mindestens alle 10 s. Pro Pumpe nur die Live-Werte: Tätigkeit, Phase, Fortschritt (`done_ml`, `target_ml`, `rpm`, `prime_s`), Behälter, Tagesmenge, letzter Lauf. Alles andere liefert `GET /api/v1/pumps`. |
| Anfragen | `{"type":"…","id"?:n,…}` → Antwort mit gleichem `type`/`id`, `"ok"` und bei Fehler `"error"` (Code). `{"type":"ping"}` zum Testen. |
| Entlüften | `{"type":"prime","pump":1,"action":"start\|beat\|stop","tier"?:0..2}`. Beim Halten alle 150 ms `beat`. Bricht die Verbindung ab, stoppt die Pumpe sofort (zusätzlich nach `prime_heartbeat_ms` ohne Heartbeat). |
| Sicherheit | Browser nur von den eigenen Seiten (Origin = Host, sonst 403 `forbidden_origin`). Max. 4 Verbindungen (503 `too_many_clients`). Abmelden, Passwortwechsel oder gelöschter API-Key beenden die Verbindung (Prüfung alle 5 s und bei jeder Nachricht). |

Die Dosiertest-Seite nutzt den WebSocket (Chip „Live“) und fällt ohne ihn auf Abfragen
und REST-Entlüften zurück („Abfrage“).

## Weboberfläche (M5)

`http://lgal-doser-xxxx.local/` öffnet die Oberfläche (Anmeldung wie unter [Zugang](#zugang)).
Sie ist in die Firmware eingebettet: `components/webui/www/` wird beim Build von `pack.py`
gzip-komprimiert (≈ 82 KB) und mit der Firmware per OTA aktualisiert – Oberfläche und API
passen so immer zusammen, und der Rollback gilt auch für die Oberfläche. Preact und htm liegen
unverändert in `www/vendor/` (Versionen und Prüfsummen dort im README); einen Node-Build gibt es
nicht, `idf.py build` reicht.

| Thema | Verhalten |
|---|---|
| Seiten | Übersicht, Pumpe (Detail mit Zeitplan), Verlauf, Einstellungen, Wartung (`/#/maintenance`, aus den Einstellungen und dem Fuß). Adressen als Hash-Routen, z. B. `/#/pump/3` oder `/#/history/3` (Ereignisse vorgefiltert auf Pumpe 3); alte Links `/#/schedule/3` öffnen die Pumpe. |
| Übersicht | Pro Pumpe: Behälter-Grafik mit Füllstand (Rand gelb ab 25 %, rot ab 10 %, gestrichelt ohne Behälter-Erfassung), heute dosiert / geplant, Reichweite in Tagen (aus dem Zeitplan), nächste Dosis, laufende Dosierung und Hinweise (Treiberfehler im letzten Lauf, Behälter fast leer, Kalibrierung, Tageslimit, Schlauch, Sperrzeit, ausgelassene Dosen). |
| Pumpe | Große Behälter-Grafik mit „Nachgefüllt“ und „Ändern“ (Größe 0 = ohne Erfassung), jetzt dosieren mit Fortschritt und Stopp (Sperrzeit: Meldung mit Ende bzw. „startet danach“), Schlauch füllen per Halten (WebSocket mit Totmann, sonst REST), Kalibrier-Assistent (Start → Messbecher → gemessene Menge übernehmen oder verwerfen), Verbrauch (heute, geplant, nächste Dosis, letzter Lauf, gesamt, Schlauchstunden mit Zurücksetzen) und Einstellungen. Name und Betrieb (automatisch / nur manuell / aus) für alle; Geschwindigkeit beim Dosieren, Zielmenge der Kalibrierung und Feintuning nur in Profi. Einfach zeigt aktive Profi-Werte als Hinweis. |
| Zeitplan | Karte im Pumpen-Detail: die Dosen von heute auf einer 24-h-Leiste mit aktueller Uhrzeit, darunter der Plan – genau einer pro Pumpe. **Einfach:** Tagesmenge in N Dosen zwischen zwei Uhrzeiten, täglich, mit Vorschau der Uhrzeiten; ein in Profi angelegter Plan wird nur zusammengefasst und bleibt aktiv (bearbeiten in Profi oder durch einfachen Plan ersetzen). **Profi:** Planart „Verteilt“ (Tagesmenge in N Dosen) oder „Einzelzeiten“ (bis 24 Uhrzeiten mit eigener Menge), täglich / Wochentage / alle N Tage, pausierbar, sowie das Verhalten nach Stromausfall. Die Leiste zeigt ungespeicherte Änderungen sofort; Raster-, Tageslimit- und Sperrzeit-Konflikte werden mit Uhrzeit bzw. Pumpen und Uhrzeiten gemeldet. Für alle: Zeitplan der Pumpe als JSON-Datei exportieren und (auch auf eine andere Pumpe) importieren; das Gerät prüft den importierten Plan wie einen bearbeiteten. |
| Verlauf | Für alle: dosierte Menge pro Tag als kleines Balkendiagramm je Pumpe (7, 14, 30 oder 60 Tage) mit Summe und Tagesmittel; Zeigen oder Tippen auf einen Balken nennt Tag, Menge, Anzahl Dosen und ausgelassene Dosen (Punkt unter dem Balken), dazu eine Tabellenansicht. Nur Profi: Ereignisliste (Dosen, ausgelassene Dosen, Füllen, Kalibrierung, Behälter, Schlauch, Alarme) mit Filter nach Pumpe, Art und Zeitraum, „Mehr laden“ und CSV-Export. Das Pumpen-Detail verlinkt unter „Verbrauch“ auf den Verlauf der Pumpe. |
| Boost | Karte im Pumpen-Detail (Einfach und Profi): Zusatzmenge über 1–30 Tage mit Vorschau „≈ X ml zusätzlich pro Tag“, Hinweis bei fehlendem Zeitplan, Betrieb ≠ automatisch oder Überschreiten des Tageslimits. Ein laufender Boost zeigt Rest, Fortschritt und Enddatum und lässt sich beenden; Übersicht und Kopfzeile zeigen ihn ebenfalls. |
| Sperrzeiten | Nur Profi, in den Einstellungen: Pumpenpaare mit Mindestabstand in Minuten (bis 10 Regeln) und ob eine manuelle Dosis in der Sperrzeit abgelehnt wird oder wartet. Das Gerät prüft beim Speichern alle Zeitpläne gegen die Regeln und meldet Kollisionen. |
| Uhrzeit | In den Einstellungen: Gerätezeit und Quelle (Zeitserver, Uhrenmodul, von Hand), Warnung bei mehr als 2 min Abweichung zum Browser oder stummem Uhrenmodul, „Uhrzeit von diesem Browser übernehmen“. Zeitzone (Liste wie im Einrichtungsassistenten oder POSIX-Angabe, Vorschlag aus dem Browser) für alle; Zeitserver (NTP) und letzter Abgleich nur in Profi. |
| Status-LED | In den Einstellungen für alle: was die LED gerade zeigt, die Bedeutung der Farben und der Nachtmodus (Zeitfenster, Bereit-Licht nachts aus / 10 / 25 / 50 %). |
| Netzwerk | In den Einstellungen: Status (aktive Schnittstelle, LAN- und WLAN-Adresse, Signal, Setup-WLAN, Countdown bis zum WLAN-Ersatz) für alle. Nur Profi: Schnittstelle (automatisch / nur LAN / nur WLAN), DHCP oder feste Adressen je Schnittstelle (vorbelegt mit den aktuellen), WLAN-Suche, Passwort, Verbindungstest und Speichern; ändert sich die feste LAN-Adresse, nennt die Seite die neue. `/network` bleibt als Notfallseite. |
| Anmeldung | In den Einstellungen: Passwort ändern (bzw. erstmals festlegen) und Abmelden für alle; API-Schlüssel (Liste, erzeugen mit einmaliger Anzeige und Kopieren, löschen) nur in Profi. `/login` bleibt für Anmeldung, Passwort vergessen und Engineering. |
| Wartung | Firmware-Version, Erstellungsdatum, Adresse, Laufzeit und Hinweis nach einem Rollback für alle; Firmware-Update per `.bin`-Upload mit Fortschritt (gesperrt, solange eine Pumpe läuft), danach wartet die Seite auf den Neustart und lädt neu; Neustart mit Rückfrage. Nur Profi: Speicherplatz und OTA-Zustand, MAC, letzter Neustart-Grund, Speicher, Log und Absturzbericht (laden, löschen). |
| Ansicht | **Einfach** / **Profi**, gespeichert pro Browser. Nur in Profi: detaillierte Zeitpläne, Pumpen-Feintuning (Stufe, Rückzug, Tageslimit), Regeln zwischen Pumpen (Sperrzeiten, Vormerken, Stromausfall) sowie Netzwerk-Einstellungen, Zeitserver und API-Keys. In Profi Eingestelltes bleibt in Einfach aktiv. |
| Live-Werte | Per WebSocket; ohne Verbindung fragt die Seite jede Sekunde ab. |
| Darstellung | Dunkel, hell oder wie das System; Sprache wie auf allen Geräteseiten. |
| Service | 7× schnell auf die Firmware-Version im Fuß klicken → Dialog für den Engineering-Schlüssel; freigeschaltet zeigt er Restzeit, die Serviceseiten (Dosier-, Zeitplan-, Hardwaretest), die [Datensicherung](#datensicherung-m6) (herunterladen mit oder ohne Passwörter, aus Datei wiederherstellen) und „Jetzt sperren“. |
| Notfall | `/recovery` bleibt als Wartungsseite ohne Weboberfläche (Firmware-Upload, Neustart, Log, Testseiten). |
| Dateien | `/ui/…` ohne Anmeldung (nur Programmcode, keine Gerätedaten), ETag und `no-cache`. |

## OTA-Update

In der Weboberfläche unter Wartung, über die Notfallseite (`/recovery`) per Datei-Upload oder per Skript:

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
| GET | `/` | Weboberfläche (M5); im Werkszustand Weiterleitung auf `/setup` |
| GET | `/recovery` | Wartungsseite (Notfall, im Firmware-Image) |
| GET | `/ui/…` | Dateien der Weboberfläche (ohne Anmeldung) |
| GET | `/login` | Anmeldung, Passwort, API-Keys, Engineering |
| GET | `/network` | Netzwerk-Seite |
| GET | `/api/v1/net/status` | aktive Schnittstelle, LAN, WLAN (Zustand, RSSI, Fehlergrund), Setup-WLAN, `fallback_in_s` |
| GET / PUT | `/api/v1/net/settings` | Einstellungen (JSON, siehe [Netzwerk](#netzwerk-m4)); PUT nimmt Teil-Dokumente |
| POST / GET | `/api/v1/net/scan` | WLAN-Suche starten / Ergebnis (`idle\|running\|done\|failed`, stärkstes zuerst) |
| POST / GET | `/api/v1/net/wlan/test` | Verbindungstest `{"ssid","password"?,"dhcp",…}` / Ergebnis (`ok` mit `ip`, `failed` mit `reason`) |
| GET | `/api/v1/auth/status` | angemeldet?, Passwort gesetzt?, Engineering-Restzeit, Wiederherstellung möglich? |
| POST | `/api/v1/auth/login` | `{"password"}` → Sitzungs-Cookie |
| POST | `/api/v1/auth/logout` | Sitzung beenden |
| POST | `/api/v1/auth/password` | `{"current","new"}` (vor dem ersten Setzen ohne `current`) |
| POST | `/api/v1/auth/recover` | `{"device_password","new"}`, nur aus dem Setup-WLAN |
| GET / POST / DELETE | `/api/v1/auth/keys[?slot=1]` | API-Keys auflisten / `{"name"}` anlegen / löschen |
| POST / DELETE | `/api/v1/auth/engineering` | `{"key"}` freischalten / sperren |
| GET | `/api/v1/system/info` | Version, Partition, OTA-Status, Netz, Speicher, Zeit, Sprache, Links |
| GET / POST | `/api/v1/system/language?lang=en\|de` | Sprache der Oberfläche |
| GET / PUT | `/api/v1/system/led` | angezeigter Zustand (`shown`) und Nachtmodus / `{"night":{"enabled","start":"22:00","end":"07:00","brightness":0..100}}` (fehlende Felder bleiben, 400 `invalid_param` mit `field`) |
| GET / PUT | `/api/v1/system/time` | Uhrzeit, Quelle, Zeitzone, NTP-Server / `{"timezone"?,"ntp_server"?,"utc"?}` (alles geprüft, bevor etwas übernommen wird; 400 `invalid_param` mit `field`) |
| GET | `/setup` | Ersteinrichtung |
| GET | `/api/v1/setup` | `completed`, `password_set`, `connected`, `hostname`, `urls` |
| POST | `/api/v1/setup/complete` | Einrichtung abschließen (409 `password_not_set` / `not_connected`), Setup-WLAN aus nach 20 s |
| GET | `/i18n.js` | gemeinsame Übersetzungsbasis der Seiten (ohne Anmeldung) |
| GET | `/api/v1/system/log` | RAM-Log der letzten ~32 KB |
| POST | `/api/v1/system/reboot` | Neustart |
| GET / DELETE | `/api/v1/system/coredump` | letzter Absturz-Dump (ELF) / löschen |
| POST | `/api/v1/ota/firmware` | Firmware-Upload (rohe `.bin`), abgelehnt solange eine Pumpe läuft |
| GET | `/dosing` | Dosiertest-Seite |
| GET | `/api/v1/pumps` | Status, Kalibrierung, Rückzug, Limit, Behälter, Schlauch aller Pumpen |
| POST | `/api/v1/pumps/dose?pump=1&ml=1.5` | Dosieren (optional `tier=0..2`). Während einer Sperrzeit: 409 `collision_lock` mit `retry_at`/`retry_local`, oder 202 `queued` (Sperrzeit-Regel „vormerken“) |
| POST | `/api/v1/pumps/stop?pump=0..5` | sanfter Stopp, Rückzug läuft noch (0 = alle); verwirft eine vorgemerkte Dosis |
| POST | `/api/v1/pumps/prime?pump=1&action=start\|beat\|stop` | Entlüften mit Totmann-Heartbeat (besser per WebSocket) |
| GET | `/api/v1/ws` | WebSocket: Status-Push und Entlüften (siehe [WebSocket](#websocket-m4)) |
| POST | `/api/v1/pumps/calibrate?pump=1&action=start[&ml=20]` | Kalibrierlauf; danach `action=apply&measured_ml=…` oder `cancel` |
| POST | `/api/v1/pumps/settings?pump=1&…` | `name`, `mode=auto\|manual_only\|disabled`, `tier`, `daily_limit_ml`, `retract_rev` (Umdrehungen, `off` = 0 = aus, `global` = Service-Wert; ab Werk aus) |
| POST | `/api/v1/pumps/container?pump=1&…` | `capacity_ml`, `remaining_ml` (nur Kapazität = neuer, voller Behälter) |
| POST | `/api/v1/pumps/tube?pump=1&hours=0` | Schlauchzähler setzen/zurücksetzen |
| GET | `/api/v1/history?…` | Ereignisse, neueste zuerst: `pump=1..5`, `kind=dose,skip,…`, `days=1..60` (oder `from`/`to` in UTC-Sekunden), `limit=1..500` (100), `offset` → `{"events":[…],"more"}` (siehe [Historie](#historie-m6)) |
| GET | `/api/v1/history.csv?…` | dieselben Filter als CSV-Download, ohne Limit |
| GET | `/api/v1/history/daily?days=14` | dosierte Menge, Anzahl Dosen und ausgelassene Dosen je Ortstag und Pumpe (1–60 Tage, älteste zuerst; ohne gültige Uhrzeit leer) |
| DELETE | `/api/v1/history` | Historie löschen (Engineering) |
| GET | `/api/v1/backup?secrets=0\|1` | Datensicherung der ganzen Konfiguration (Engineering, siehe [Datensicherung](#datensicherung-m6)) |
| POST | `/api/v1/backup?parts=system,network,pumps,engineering,auth` | Sicherung einspielen (Engineering); erst alles prüfen, dann die gewählten Teile übernehmen; 409 `pump_busy` solange eine Pumpe läuft |
| GET / PUT | `/api/v1/mqtt` | MQTT-Einstellungen (ohne Passwort, `password_set`), Basis-Topic und Port in Gebrauch, Verbindungsstatus `disabled\|connecting\|connected` mit letztem Fehler, CA-Info; PUT teilweise, Passwort weglassen = behalten, `""` = löschen |
| PUT / DELETE | `/api/v1/mqtt/ca` | CA-Zertifikat(e) als PEM-Text im Body (max. 8 KB, wird geprüft) bzw. entfernen (409 `ca_in_use`, solange TLS „eigenes CA-Zertifikat“ aktiv ist) |
| GET / POST | `/api/v1/engineering/dosing?…` | Super-Admin: `retract_rev`, `calib_ml`, `tier1_rpm`…`tier3_rpm`, `prime_max_s`, `prime_heartbeat_ms`, `min_dose_ml`, `tube_life_h`, `limit_mode=calendar_day\|rolling_24h` |
| GET | `/schedule` | Zeitplantest-Seite |
| GET / PUT | `/api/v1/pumps/schedule?pump=1` | Zeitplan einer Pumpe (JSON, siehe unten) + Zeitraster `grid_min` + heutige Dosen |
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
| Uhr | DS3231 speichert UTC. Start: Zeit aus RTC. NTP stellt die RTC nach. Ohne NTP stündlicher Abgleich aus der RTC. Zeitzone POSIX (`CET-1CEST,M3.5.0,M10.5.0/3`) und NTP-Server (Standard `pool.ntp.org`, leer = aus) über `/api/v1/system/time` einstellbar, gespeichert in NVS. |
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
| Ablauf | Rückzug ausgleichen → Hauptfahrt → Anti-Tropf-Rückzug. Der ausgeführte Rückzug wird spannungsausfallsicher gespeichert. Der Rückzug ist pro Pumpe **ab Werk aus** und lässt sich auf „Standard“ (globaler Wert) oder einen eigenen Wert stellen; beim Ausschalten wird ein offener Rückzug bei der nächsten Dosis noch ausgeglichen. |
| Verbuchung | Beim Start wird die volle Menge auf Tageslimit, Behälter und Gesamtzähler gebucht und gesichert; ein Abbruch schreibt die nicht geförderte Menge gut. Stromausfall während der Fahrt → Menge bleibt gebucht (sichere Seite). |
| Tageslimit | hart pro Pumpe; Fenster Kalendertag oder rollierend 24 h (Engineering). Stunden-Töpfe über 26 h, angebrochene Stunden zählen voll. Ohne gültige Uhrzeit werden Pumpen mit Limit gesperrt. |
| Kalibrierung | Lauf mit Zielmenge (Standard 20 ml), dann gemessene Menge eintragen. Plausibel: 0,25- bis 4-facher Nennwert. Die aufgefangene Menge wird vom Behälter abgezogen. |
| Entlüften | läuft, solange der Button gehalten wird (Heartbeat alle 150 ms, Stopp nach 500 ms ohne Heartbeat oder sofort bei Abbruch der WebSocket-Verbindung, max. 60 s). Zählt nicht für Behälter/Limit, aber für die Schlauch-Laufzeit. |
| Sperren | Betriebsart `disabled`, OTA-Update, Treiberfehler. |
| Speicher | Einstellungen in NVS (`dose`). Laufzustand je Pumpe im EEPROM als A/B-Datensätze (Folgenummer + CRC32, ab 0x000), Rückfall auf NVS ohne EEPROM. |

Engineering-Standardwerte (Super-Admin, global; Rückzug zusätzlich pro Pumpe):
Rückzug „Standard“ ¼ U (≈ 0,09 ml), Kalibrier-Zielmenge 20 ml, Stufen 100/200/300 U/min
(≈ 37/74/111 ml/min), Entlüften max. 60 s / Heartbeat 500 ms, kleinste Dosis 0,02 ml,
Schlauch-Lebensdauer 1000 h, Tageslimit pro Kalendertag. Änderungen brauchen die
Engineering-Freischaltung (siehe [Zugang](#zugang)).

Die Testseite **`/dosing`** bedient alles davon pro Pumpe; unten liegen die
Engineering-Parameter.

## Zeitplan (M3)

| Thema | Umsetzung |
|---|---|
| Plan | genau einer pro Pumpe: „verteilt“ („X ml in N Dosen zwischen A und B“, gleichmäßig auf das Raster gelegt, Rundungsrest auf die ersten Dosen, höchstens 96 Dosen pro Tag) oder „Einzelzeiten“ (bis 24 Uhrzeiten, jede mit eigener Menge). Tage, Pause und Stromausfall-Verhalten gelten für den ganzen Plan. |
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
| Speicher | Pläne, Sperrzeiten, Parameter in NVS (`sched`). Fortschritt je Pumpe (zuletzt bearbeitete Dosis, Boost, Zähler) im EEPROM als A/B-Datensätze ab 0x640, Rückfall auf NVS. Geschrieben wird nur, wenn eine Dosis bearbeitet wurde. |

Zeitplan als JSON (`PUT /api/v1/pumps/schedule?pump=1`; unbekannte Felder werden ignoriert, ein
GET-Ergebnis kann also bearbeitet zurückgeschickt werden):

```json
{"kind": "spread", "enabled": true, "start": "08:00", "end": "20:00", "count": 12, "ml": 24,
 "days": {"mode": "daily"}, "power_loss": "skip_missed", "catch_up_min": 60}

{"kind": "times", "times": [{"time": "09:00", "ml": 5}, {"time": "21:30", "ml": 2.5}],
 "days": {"mode": "interval", "every": 2, "anchor": "2026-11-06"}}

{"kind": "none"}
```

Sperrzeiten (`PUT /api/v1/schedule/locks`):

```json
{"manual": "reject", "rules": [{"pumps": [1, 2], "gap_min": 5}]}
```

`"end"` braucht es nur bei mehr als einer Dosis; Einzelzeiten werden beim Speichern sortiert,
`"enabled": false` pausiert den Plan. Fehler nennen die Stelle (`"entry": 2, "field": "ml"` für
die zweite Uhrzeit in sortierter Reihenfolge) bzw. bei 409 `conflict` die
kollidierenden Dosen (`"conflicts": [{"code": "collision_lock", "first": {…}, "second": {…}, "gap_min": 5}]`).
Zeitpunkte kommen als UTC-Sekunden plus Ortszeit des Geräts (`"at"` + `"local"`), weil der
Browser in einer anderen Zeitzone sein kann.

Die Testseite **`/schedule`** zeigt den Status aller Pumpen (inkl. Boost und manueller Dosis)
und bearbeitet Zeitpläne, Sperrzeiten und die Scheduler-Parameter.

## Historie (M6)

Das Gerät zeichnet Ereignisse als 24-Byte-Einträge auf der LittleFS-Partition `storage` auf
(eine Datei pro UTC-Tag unter `/storage/hist`, Prüfsumme je Eintrag). Tage älter als 60 Tage
löscht es stündlich. Geschrieben wird über eine Warteschlange in einem eigenen Task – Dosierung
und Zeitplan warten nie auf den Flash. Ohne gültige Uhrzeit wird nichts aufgezeichnet.
Bei rund 240 Dosen am Tag sind das etwa 6 KB pro Tag, 60 Tage ≈ 350 KB von 5,9 MB.
Werkseinstellungen (Taster 10 s) löschen die Historie mit.

| `kind` | Inhalt |
|---|---|
| `dose`, `prime`, `calib_run` | Ziel- und Ist-Menge, Ergebnis (`success`, `aborted`, `fault`, `locked`, `heartbeat_lost`, `max_duration`), Auslöser (`schedule`, `manual`, `api`, `boost`, `mqtt`) |
| `skip` | ausgelassene Dosis mit Grund (`missed`, `daily_limit`, `delay_timeout`, …); nach einem Stromausfall ein Eintrag mit `count` |
| `container` | Behälter gesetzt oder nachgefüllt (`remaining_ml`, `capacity_ml`) |
| `calibration` | übernommene Kalibrierung (`ul_per_rev`, `measured_ml`, `target_ml`) |
| `tube` | Schlauchzähler gesetzt (`hours`) |
| `alarm` | Beginn eines Alarms: `container_low`, `tube_worn`, `rtc_fault` (Pumpe 0 = Gerät); Treiberfehler und Tageslimit stehen schon als Ergebnis bzw. ausgelassene Dosis drin |

Die CSV hat die Spalten `utc,local,kind,pump,source,code,target_ml,done_ml,value`; `value` ist
der Inhalt (Behälter), µl pro Umdrehung (Kalibrierung), Stunden (Schlauch) oder die Anzahl
(ausgelassen).

## Datensicherung (M6)

**Zeitplan pro Pumpe** (alle): im Pumpen-Detail als JSON-Datei exportieren und importieren, auch
auf eine andere Pumpe (`{"format":"lgal-doser-schedule","version":1,…,"schedule":{…}}`).

**Ganzes Gerät** (nur Engineering, Service-Dialog): eine Datei mit Sprache, Zeitzone,
Zeitserver, LED-Nachtmodus, Netzwerk, allen Pumpen-Einstellungen inkl. Kalibrierung, Zeitplänen,
Sperrzeiten, Servicewerten und Hardware-Parametern (Ströme, Mikroschritte).
Behälterinhalt, Zähler und Verlauf gehören nicht dazu. Beim Export wählbar:

| Variante | Inhalt |
|---|---|
| ohne Passwörter | kein WLAN-Passwort, keine Zugangsdaten; beim Einspielen bleibt das gespeicherte WLAN-Passwort, wenn das Netz (SSID) gleich ist |
| mit Passwörtern | zusätzlich WLAN-Passwort (Klartext) sowie Admin-Passwort und API-Keys (nur als PBKDF2- bzw. SHA-256-Hash, wie im Gerät gespeichert) |

Beim Einspielen zeigt der Dialog Gerät, Firmware und Datum der Sicherung und lässt die Teile
wählen; Netzwerk und Zugangsdaten sind vorab abgewählt. Die Firmware prüft das ganze Dokument
(Format, Version, jeden Wert wie die einzelnen Endpunkte) bevor sie etwas übernimmt, und lehnt
es ab, solange eine Pumpe läuft. Zugangsdaten melden alle ab; ein neues Netzwerk gilt 2 s nach
der Antwort. Der Engineering-Schlüssel (Hersteller) ist nie Teil der Sicherung.

## Taster und LED

| Taster (GPIO0) | Aktion beim Loslassen | LED während des Haltens |
|---|---|---|
| ≥ 3 s | Setup-WLAN für 30 min (auch zum Zurücksetzen des Admin-Passworts) | Orange |
| ≥ 10 s | Werkseinstellungen (NVS + Datenpartition löschen) | Rot |

Beim Einschalten gedrückt = ESP-Download-Modus (Hardware). Ein dauerhaft gedrückter
Taster löst nichts aus, solange er nicht einmal losgelassen wurde.

LED-Zustände, höchste Priorität zuerst:

| Farbe | Bedeutung |
|---|---|
| Rot, schnell blinkend | Alarm: heute eine geplante Dosis wegen Tageslimit ausgelassen, letzter Lauf mit Treiberfehler, Uhrenmodul antwortet nicht |
| Magenta/Weiß | Firmware-Update läuft |
| Violett pulsierend | Kalibrierung oder Entlüften |
| Cyan pulsierend | Pumpe läuft |
| Orange pulsierend | Setup-WLAN an |
| Gelb, langsam blinkend | Warnung: Behälter ≤ 10 % oder < 3 Tage, Schlauch-Lebensdauer erreicht, kein Netz, keine gültige Uhrzeit |
| Grün / Türkis | bereit, per LAN / WLAN verbunden |
| Weiß aufblendend | Start |

Ausgeschaltete Pumpen (Betrieb „aus“) lösen weder Alarm noch Warnung aus. Die Regel steht in
`doser/alarms.h` (Host-getestet); `main.cpp` wertet sie alle 2 s aus.

**Nachtmodus** (Einstellungen → Status-LED oder `PUT /api/v1/system/led`): dimmt das Bereit-Licht
(Grün/Türkis) in einem Zeitfenster der Ortszeit, z. B. 22:00–07:00, oder schaltet es aus. Alarme,
Warnungen und laufende Vorgänge bleiben voll sichtbar; ohne gültige Uhrzeit bleibt das Licht hell.
Ab Werk aus.

## Meilensteine

- [x] **M0** Grundgerüst: Partitionen, Ethernet, Setup-WLAN, mDNS, OTA + Rollback, Wartungsseite, LED, Taster
- [x] **M1** Hardware-Ebene: TMC2209-UART, Schritt-Takte (RMT + MCPWM/PCNT), RTC, Raw-Stepper-Test
- [x] **M2** Dosier-Engine: Rückzug, Kalibrierung, Priming, Behälter, Tageslimit, Schlauch-Laufzeit; Mehrsprachigkeit (en/de)
- [x] **M3** Zeitplan, Zeitumstellung, Kollisionsschutz, Boost, Verhalten nach Stromausfall (mit Host-Tests)
- [x] **M4** WLAN-Client, Captive Portal, Ersteinrichtung, Auth, REST, WebSocket
- [ ] **M5** WebUI (SPA, eingebettet ins Firmware-Image)
- [ ] **M6** MQTT + Home-Assistant-Discovery, Historie, Backup, Engineering-Modus
