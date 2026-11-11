# LGAL-Doser – Firmware

Dosierpumpe „LG Aquatic Life“ (erstes Gerät: ESP32-S3-DEV-KIT-N16R8, 5× TMC2209, W5500).
Hardware-Beschreibung: [CLAUDE.md](CLAUDE.md).

Framework: **ESP-IDF v6.0.3**, C++ (gnu++2b), FreeRTOS.

## Struktur

```
components/lgal_core/   LG Aquatic Life Core: Netz, HTTP, OTA, LED, Taster, Einstellungen, Log
components/doser_hal/   Pinbelegung, sicherer Start, TMC2209-UART, Schritt-Takte, Pumpen, RTC/EEPROM
components/doser_app/   Geräte-Logik: Hardware-Diagnose, Dosier-Engine, Zeitplan (Scheduler), Engineering und ihre REST-APIs
components/webui/       Weboberfläche (M5): www/ (Preact + htm, ohne Build), pack.py bettet sie gzip-komprimiert ein
main/                   Verdrahtung der Komponenten (app_main)
tools/                  ap_password.py, ota_upload.py, mock/ (Geräte-Mock für die Weboberfläche)
tests/host/             Host-Tests (TMC-Protokoll, Strom, Rampe, Kalender, Zeitzonen, JSON, Dosier- und Zeitplan-Logik, LED-Alarme, Historie, Datensicherung, MQTT/Home Assistant)
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
  Die Weboberfläche sperrt beim Start eine Freischaltung, die nicht aus demselben
  Browser-Tab stammt – ein geschlossener Tab bleibt damit gesperrt.

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
| Werkseinstellungen | Taster 10 s, Wartungsseite (Admin-Passwort erneut eingeben) oder Engineering-Menü löscht alles (siehe [Taster und LED](#taster-und-led)) → Ersteinrichtung beginnt von vorn. |

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

Die Weboberfläche nutzt den WebSocket für Live-Werte und das Entlüften und fällt ohne ihn auf
Abfragen und REST-Entlüften zurück.

## Weboberfläche (M5)

`http://lgal-doser-xxxx.local/` öffnet die Oberfläche (Anmeldung wie unter [Zugang](#zugang)).
Sie ist in die Firmware eingebettet: `components/webui/www/` wird beim Build von `pack.py`
gzip-komprimiert (≈ 104 KB) und mit der Firmware per OTA aktualisiert – Oberfläche und API
passen so immer zusammen, und der Rollback gilt auch für die Oberfläche. Preact und htm liegen
unverändert in `www/vendor/` (Versionen und Prüfsummen dort im README); einen Node-Build gibt es
nicht, `idf.py build` reicht.

| Thema | Verhalten |
|---|---|
| Seiten | Übersicht, Pumpe (Detail mit Zeitplan), Verlauf, Einstellungen, Wartung (`/#/maintenance`, aus den Einstellungen). Adressen als Hash-Routen, z. B. `/#/pump/3` oder `/#/history/3` (Ereignisse vorgefiltert auf Pumpe 3); alte Links `/#/schedule/3` öffnen die Pumpe. |
| Übersicht | Pro Pumpe: Behälter-Grafik mit Füllstand (Rand gelb, wenn er weniger Tage reicht als die Warnschwelle, Standard 7, rot unter der Alarmschwelle, Standard 4; beide Schwellen pro Pumpe einstellbar, gestrichelt ohne Behälter-Erfassung), heute dosiert / geplant, Reichweite in Tagen (aus dem Zeitplan), nächste Dosis, laufende Dosierung und Hinweise (Treiberfehler im letzten Lauf, Behälter fast leer, Kalibrierung, Tageslimit, Schlauch, Sperrzeit, ausgelassene Dosen). |
| Pumpe | Behälter-Karte als Übersicht: große Behälter-Grafik, heute dosiert / geplant, Reichweite, nächste Dosis (mit Boost-Anteil) und Hinweise wie auf der Übersicht, dazu „Nachgefüllt“ und „Ändern“ (Größe 0 = ohne Erfassung); jetzt dosieren mit Fortschritt und Stopp (Sperrzeit: Meldung mit Ende bzw. „startet danach“), Schlauch füllen per Halten (WebSocket mit Totmann, sonst REST), Kalibrier-Assistent (Start → Messbecher → gemessene Menge übernehmen oder verwerfen), Verbrauch (Tageslimit, letzter Lauf, gesamt, Schlauchstunden mit Zurücksetzen) und Einstellungen. Name und Betrieb (automatisch / nur manuell / aus) für alle; Geschwindigkeit beim Dosieren, Zielmenge der Kalibrierung und Feintuning nur in Profi. Einfach zeigt aktive Profi-Werte als Hinweis. |
| Zeitplan | Karte im Pumpen-Detail: die Dosen von heute auf einer 24-h-Leiste mit aktueller Uhrzeit, darunter der Plan – genau einer pro Pumpe. **Einfach:** Tagesmenge in N Dosen zwischen zwei Uhrzeiten, täglich, mit Vorschau der Uhrzeiten; ein in Profi angelegter Plan wird nur zusammengefasst und bleibt aktiv (bearbeiten in Profi oder durch einfachen Plan ersetzen). **Profi:** Planart „Verteilt“ (Tagesmenge in N Dosen) oder „Einzelzeiten“ (bis 24 Uhrzeiten mit eigener Menge), täglich / Wochentage / alle N Tage, sowie das Verhalten nach Stromausfall. Ob der Plan läuft, bestimmt der Betrieb der Pumpe (automatisch / nur manuell). Die Leiste zeigt ungespeicherte Änderungen sofort; Raster-, Tageslimit- und Sperrzeit-Konflikte werden mit Uhrzeit bzw. Pumpen und Uhrzeiten gemeldet. In Profi: Zeitplan der Pumpe als JSON-Datei exportieren und (auch auf eine andere Pumpe) importieren; das Gerät prüft den importierten Plan wie einen bearbeiteten. |
| Verlauf | Für alle: dosierte Menge pro Tag als kleines Diagramm je Pumpe (7, 14, 30 oder 60 Tage), wahlweise Linie (Standard) oder Balken (im Browser gemerkt), mit beschrifteten Achsen (ml; Wochentage bzw. Datum, „heute“ immer beschriftet), Summe und Tagesmittel; Zeigen oder Tippen nennt Tag, Menge, Anzahl Dosen und ausgelassene Dosen (Punkt unter der Achse), dazu eine Tabellenansicht. Nur Profi: Ereignisliste (Dosen, ausgelassene Dosen, Füllen, Kalibrierung, Behälter, Schlauch, Alarme) mit Filter nach Pumpe, Art und Zeitraum, „Mehr laden“ und CSV-Export. Das Pumpen-Detail verlinkt unter „Verbrauch“ auf den Verlauf der Pumpe. |
| Boost | Karte im Pumpen-Detail (Einfach und Profi): Zusatzmenge über 1–30 Tage mit Vorschau „≈ X ml zusätzlich pro Tag“, Hinweis bei fehlendem Zeitplan, Betrieb ≠ automatisch oder Überschreiten des Tageslimits. Ein laufender Boost zeigt Rest, Fortschritt und Enddatum und lässt sich beenden; Übersicht und Kopfzeile zeigen ihn ebenfalls. |
| Sperrzeiten | Nur Profi, in den Einstellungen: Pumpenpaare mit Mindestabstand in Minuten (bis 10 Regeln) und ob eine manuelle Dosis in der Sperrzeit abgelehnt wird oder wartet. Das Gerät prüft beim Speichern alle Zeitpläne gegen die Regeln und meldet Kollisionen. |
| Uhrzeit | In den Einstellungen: Gerätezeit und Quelle (Zeitserver, Uhrenmodul, von Hand), Warnung bei mehr als 2 min Abweichung zum Browser, stummem Uhrenmodul oder verlorener Uhrzeit (Pufferbatterie), „Uhrzeit von diesem Browser übernehmen“. Zeitzone (Liste wie im Einrichtungsassistenten oder POSIX-Angabe, Vorschlag aus dem Browser) für alle; Zeitserver (NTP) und letzter Abgleich nur in Profi. |
| Status-LED | In den Einstellungen für alle: was die LED gerade zeigt, die Bedeutung der Farben und der Nachtmodus (Zeitfenster, Bereit-Licht nachts aus / 10 / 25 / 50 %). |
| Netzwerk | In den Einstellungen: Status (aktive Schnittstelle, LAN- und WLAN-Adresse, Signal, Setup-WLAN, Countdown bis zum WLAN-Ersatz) für alle. Nur Profi: Schnittstelle (automatisch / nur LAN / nur WLAN), DHCP oder feste Adressen je Schnittstelle (vorbelegt mit den aktuellen), WLAN-Suche, Passwort, Verbindungstest und Speichern; ändert sich die feste LAN-Adresse, nennt die Seite die neue. `/network` bleibt als Notfallseite. |
| MQTT | In den Einstellungen: Verbindungsstatus mit letztem Fehler (Hostname, nicht erreichbar, TLS, Anmeldung) für alle. Nur Profi: Broker und Port, Verschlüsselung (keine / TLS mit öffentlichem Zertifikat / TLS mit eigenem CA-Zertifikat inkl. Hochladen, Ersetzen, Entfernen), Benutzer und Passwort, Basis-Topic, Home-Assistant-Erkennung mit Präfix, „Steuerung über MQTT erlauben“ mit Warnhinweis und die Liste der Topics. Siehe [MQTT / Home Assistant](#mqtt--home-assistant-m6). |
| Anmeldung | In den Einstellungen: Passwort ändern (bzw. erstmals festlegen) und Abmelden für alle; API-Schlüssel (Liste, erzeugen mit einmaliger Anzeige und Kopieren, löschen) nur in Profi. `/login` bleibt für Anmeldung, Passwort vergessen und Engineering. |
| Wartung | Firmware-Version, Erstellungsdatum, Adresse, Laufzeit und Hinweis nach einem Rollback für alle; Firmware-Update per `.bin`-Upload mit Fortschritt (gesperrt, solange eine Pumpe läuft), danach wartet die Seite auf den Neustart und lädt neu; Neustart mit Rückfrage; Werkseinstellungen mit erneuter Eingabe des Admin-Passworts. Nur Profi: Update von einer Adresse mit Fortschritt, Update-Option (http erlauben mit Warnung), Speicherplatz und OTA-Zustand, MAC, letzter Neustart-Grund, Speicher, Log und Absturzbericht (laden, löschen). |
| Ansicht | **Einfach** / **Profi**, gespeichert pro Browser. Nur in Profi: detaillierte Zeitpläne, Pumpen-Feintuning (Stufe, Rückzug, Tageslimit), Regeln zwischen Pumpen (Sperrzeiten, Vormerken, Stromausfall) sowie Netzwerk- und MQTT-Einstellungen, Zeitserver und API-Keys. In Profi Eingestelltes bleibt in Einfach aktiv. |
| Live-Werte | Per WebSocket; ohne Verbindung fragt die Seite jede Sekunde ab. |
| Darstellung | Dunkel, hell oder wie das System; Sprache wie auf allen Geräteseiten. |
| Service | 7× schnell auf die Firmware-Version im Fuß klicken → Dialog für den Engineering-Schlüssel, danach direkt das [Engineering-Menü](#engineering-menü-m6) (ist der Bereich schon freigeschaltet, sofort). Dort liegen auch die [Datensicherung](#datensicherung-m6) und „Jetzt sperren“. |
| Notfall | `/recovery` bleibt als Wartungsseite ohne Weboberfläche (Firmware-Upload, Neustart, Log, Links auf Netzwerk und Einrichtung) für den Fall, dass die Oberfläche nicht lädt. Sie ist bewusst nirgends verlinkt: Adresse von Hand eingeben, z. B. `http://lgal-doser-xxxx.local/recovery` (Hinweis gehört in die Bedienungsanleitung). |
| Dateien | `/ui/…` ohne Anmeldung (nur Programmcode, keine Gerätedaten), ETag und `no-cache`. |

### Mock-Server (ohne Gerät)

`tools/mock/` simuliert das Gerät für die Weboberfläche – nur Python-Standardbibliothek, nur
`localhost`:

```bash
python3 tools/mock/webui_server.py
```

Danach `http://localhost:8767/` öffnen (optional: Projektordner und Port als Argumente). Die
Seiten kommen direkt aus `components/webui/www/`; nach einer Änderung reicht Neuladen. REST-API
und WebSocket laufen mit Zustand im Speicher: Pumpen mit Dosieren, Entlüften (Totmann) und
Kalibrieren, Zeitpläne, Verlauf, Netzwerk, Anmeldung, MQTT, Datensicherung und Engineering-Menü,
dazu Einrichtungsassistent (`/setup`), Anmeldeseite und Notfallseite (`/recovery`).
Testwerte: Admin-Passwort `test-admin-pw`, Engineering-Schlüssel `test-eng-key`. Der Zustand
geht beim Beenden verloren.

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

**Update von einer Adresse:** Das Gerät lädt die `.bin` selbst herunter, z. B. von einer
Release-Seite – in der Wartung (Profi), per REST oder per MQTT:

```bash
curl -H "Authorization: Bearer lgal_..." -d '{"url":"https://example.com/lgal_doser.bin"}' \
     http://lgal-doser-xxxx.local/api/v1/ota/url
```

- Ab Werk nur **https**, das Server-Zertifikat wird gegen die in der Firmware enthaltene Liste
  öffentlicher Zertifizierungsstellen geprüft. Bis zu 5 Weiterleitungen.
- **http** lässt sich unter Wartung → Update-Optionen erlauben (mit Rückfrage). Solange das
  aktiv ist, zeigt die Wartungsseite eine Warnung, und jede http-Adresse wird vor dem Start noch
  einmal bestätigt: über http kann die Firmware unterwegs unbemerkt ausgetauscht werden. Ohne
  diese Erlaubnis wird auch eine Weiterleitung von https auf http abgelehnt.
- Fortschritt und Ergebnis kommen zusätzlich auf `<basis>/ota/state` (MQTT).
- Abgelehnt, solange eine Pumpe läuft (`pump_running`) oder schon ein Update läuft; der Download
  läuft im Hintergrund, Prüfung, Neustart und Rollback wie beim Upload.

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
| GET / PUT | `/api/v1/system/time` | Uhrzeit, Quelle, Zeitzone, NTP-Server / `{"timezone"?,"ntp_server"?,"utc"?}` oder statt `utc` `"local":"2026-11-10T18:30"` (Ortszeit des Geräts; alles geprüft, bevor etwas übernommen wird; 400 `invalid_param` mit `field`) |
| GET | `/setup` | Ersteinrichtung |
| GET | `/api/v1/setup` | `completed`, `password_set`, `connected`, `hostname`, `urls` |
| POST | `/api/v1/setup/complete` | Einrichtung abschließen (409 `password_not_set` / `not_connected`), Setup-WLAN aus nach 20 s |
| GET | `/i18n.js` | gemeinsame Übersetzungsbasis der Seiten (ohne Anmeldung) |
| GET | `/api/v1/system/log` | RAM-Log der letzten ~32 KB |
| POST | `/api/v1/system/reboot` | Neustart |
| GET / DELETE | `/api/v1/system/coredump` | letzter Absturz-Dump (ELF) / löschen |
| POST | `/api/v1/system/factory_reset` | `{"password":"…"}` – Werksreset mit Neustart; nur mit Browser-Sitzung (kein API-Key) und Admin-Passwort, Fehlversuche zählen wie beim Login (403 `wrong_password`, 429 `too_many_attempts`) |
| POST | `/api/v1/ota/firmware` | Firmware-Upload (rohe `.bin`), abgelehnt solange eine Pumpe läuft |
| POST | `/api/v1/ota/url` | `{"url":"https://…"}` → 202 `started`, das Gerät lädt selbst; 400 `invalid_url` / `http_not_allowed`, 409 `pump_running` / `ota_in_progress` |
| GET | `/api/v1/ota/status` | `{"state":"idle\|running\|failed\|rebooting","source":"api\|mqtt","http","received","total","error"?,"detail"?}` |
| GET / PUT | `/api/v1/ota/settings` | `{"allow_http"}`, ab Werk `false` |
| GET | `/api/v1/pumps` | Status, Kalibrierung, Rückzug, Limit, Behälter, Schlauch aller Pumpen |
| POST | `/api/v1/pumps/dose?pump=1&ml=1.5` | Dosieren (optional `tier=0..2`). Während einer Sperrzeit: 409 `collision_lock` mit `retry_at`/`retry_local`, oder 202 `queued` (Sperrzeit-Regel „vormerken“) |
| POST | `/api/v1/pumps/stop?pump=0..5` | sanfter Stopp, Rückzug läuft noch (0 = alle); verwirft eine vorgemerkte Dosis |
| POST | `/api/v1/pumps/prime?pump=1&action=start\|beat\|stop` | Entlüften mit Totmann-Heartbeat (besser per WebSocket) |
| GET | `/api/v1/ws` | WebSocket: Status-Push und Entlüften (siehe [WebSocket](#websocket-m4)) |
| POST | `/api/v1/pumps/calibrate?pump=1&action=start[&ml=10]` | Kalibrierlauf; danach `action=apply&measured_ml=…` oder `cancel` |
| POST | `/api/v1/pumps/settings?pump=1&…` | `name`, `mode=auto\|manual_only\|disabled`, `tier`, `daily_limit_ml`, `retract_rev` (Umdrehungen, `off` = 0 = aus, `global` = Service-Wert; ab Werk aus), `direction=cw\|ccw` (Drehrichtung über `GCONF.shaft`, ab Werk `cw`; nur im Stillstand, sonst 409 `pump_busy`), `warn_days`/`low_days` (eigene Behälter-Schwellen in Tagen Reichweite, 0 = Service-Standard; Alarm höchstens gleich Warnung) |
| POST | `/api/v1/pumps/container?pump=1&…` | `capacity_ml`, `remaining_ml` (nur Kapazität = neuer, voller Behälter) |
| POST | `/api/v1/pumps/tube?pump=1&hours=0` | Schlauchzähler setzen/zurücksetzen |
| GET | `/api/v1/history?…` | Ereignisse, neueste zuerst: `pump=1..5`, `kind=dose,skip,…`, `days=1..60` (oder `from`/`to` in UTC-Sekunden), `limit=1..500` (100), `offset` → `{"events":[…],"more"}` (siehe [Historie](#historie-m6)) |
| GET | `/api/v1/history.csv?…` | dieselben Filter als CSV-Download, ohne Limit |
| GET | `/api/v1/history/daily?days=14` | dosierte Menge, Anzahl Dosen und ausgelassene Dosen je Ortstag und Pumpe (1–60 Tage, älteste zuerst; ohne gültige Uhrzeit leer) |
| GET / POST | `/api/v1/history/settings?keep_days=30` | Aufbewahrungszeit `keep_days` (1–60, ab Werk 60); ändern nur mit Engineering-Freischaltung |
| DELETE | `/api/v1/history` | Historie löschen (Engineering) |
| GET | `/api/v1/backup?secrets=0\|1` | Datensicherung der ganzen Konfiguration (Engineering, siehe [Datensicherung](#datensicherung-m6)) |
| POST | `/api/v1/backup?parts=system,network,pumps,engineering,mqtt,auth` | Sicherung einspielen (Engineering); erst alles prüfen, dann die gewählten Teile übernehmen; 409 `pump_busy` solange eine Pumpe läuft |
| GET / PUT | `/api/v1/mqtt` | MQTT-Einstellungen (ohne Passwort, `password_set`), Basis-Topic und Port in Gebrauch, Verbindungsstatus `disabled\|connecting\|connected` mit letztem Fehler, CA-Info; PUT teilweise, Passwort weglassen = behalten, `""` = löschen |
| PUT / DELETE | `/api/v1/mqtt/ca` | CA-Zertifikat(e) als PEM-Text im Body (max. 8 KB, wird geprüft) bzw. entfernen (409 `ca_in_use`, solange TLS „eigenes CA-Zertifikat“ aktiv ist) |
| GET / POST | `/api/v1/engineering/dosing?…` | Super-Admin: `retract_rev`, `calib_ml`, `tier1_rpm`…`tier3_rpm`, `prime_max_s`, `prime_heartbeat_ms`, `min_dose_ml`, `tube_life_h`, `limit_mode=calendar_day\|rolling_24h`, `warn_days` (Warnung, wenn der Behälter weniger Tage reicht, Standard 7), `low_days` (Alarm „fast leer“, Standard 4; je 0 = aus, Alarm höchstens gleich Warnung) |
| GET / PUT | `/api/v1/pumps/schedule?pump=1` | Zeitplan einer Pumpe (JSON, siehe unten) + Zeitraster `grid_min` + heutige Dosen |
| POST | `/api/v1/pumps/boost?pump=1&ml=30&days=3` | Boost starten; `action=stop` beendet ihn |
| GET | `/api/v1/schedule/status` | nächste Dosis (`ml` inkl. erwartetem Boost-Anteil, dieser als `boost_ml`), Verzögerung, Warteschlange, Übersprungene, Boost, Reichweite aller Pumpen |
| GET / PUT | `/api/v1/schedule/locks` | Sperrzeiten zwischen Pumpen + Regel für manuelle Dosen (JSON) |
| GET / POST | `/api/v1/engineering/scheduler?…` | Super-Admin: `grid_min` (Teiler von 1440, ≤ 60), `max_delay_min` |
| GET | `/api/v1/engineering/telemetry` | Super-Admin: Laufzeit, Neustartgrund, Chip-Temperatur, Heap (frei, Minimum, größter Block, Fragmentierung), PSRAM, NVS-Einträge, LittleFS und Historie |
| POST | `/api/v1/engineering/reset?what=network` | Super-Admin: Netzwerkeinstellungen zurücksetzen (gilt nach 1 s); den Werksreset gibt es nur in der Wartung mit Admin-Passwort |
| GET | `/api/v1/hw/status` | Treiber, Pumpen, RTC, EEPROM, UART-Statistik |
| POST | `/api/v1/hw/pump/run?pump=1&steps=±N&sps=N` | Raw-Fahrt (optional `accel`, `vstart`, `force=1`) |
| POST | `/api/v1/hw/pump/stop?pump=0..5&mode=soft\|hard` | Stopp (0 = alle) |
| POST | `/api/v1/hw/pump/config?pump=1&run_ma=…` | `run_ma`, `hold_ma`, `microsteps`, `full_steps`, `invert`, `accel`, `vstart` |
| GET | `/api/v1/hw/tmc?pump=1` | TMC2209-Rohregister |
| POST | `/api/v1/hw/time?utc=…` | Zeit manuell setzen (System + RTC) |
| POST | `/api/v1/hw/rtc/aging?offset=…&auto=0\|1` | DS3231 Aging-Offset von Hand bzw. automatischer Abgleich per NTP ein/aus (ab Werk ein) |
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
| Uhr | DS3231 speichert UTC. Start: Zeit aus RTC. NTP stellt die RTC nach, und zwar phasengenau zum Sekundenbeginn und erst ab 0,5 s Abweichung. Dazwischen misst die Firmware bei jedem NTP-Abgleich die Abweichung an der Sekundenflanke; nach mindestens einem Tag ergibt das die Drift in ppm, die den Aging-Offset korrigiert (etwa 0,1 ppm pro Schritt, abschaltbar im Engineering). Hatte das Uhrenmodul beim Start die Zeit verloren (Oszillator stand, obwohl die Firmware die Uhr schon einmal gestellt hatte), meldet `rtc_lost` in `/api/v1/system/time`: Pufferbatterie prüfen. Ohne NTP stündlicher Abgleich aus der RTC. Zeitzone POSIX (`CET-1CEST,M3.5.0,M10.5.0/3`) und NTP-Server (Standard `pool.ntp.org`, leer = aus) über `/api/v1/system/time` einstellbar, gespeichert in NVS. |
| EEPROM | AT24C32 mit Seitenschreiben + ACK-Polling. Ab M2 Laufzustand der Dosierung (siehe unten). |

Standardwerte pro Pumpe (im Engineering-Menü änderbar, in NVS gespeichert): 600 mA Laufstrom,
0 mA Haltestrom, 16 Mikroschritte, 200 Vollschritte/U (Schrittwinkel 1,8°),
Rampe 600 U/min pro s, Start 20 U/min, max. 600 U/min.

Bereich D des [Engineering-Menüs](#engineering-menü-m6) zeigt Treiberstatus, Rohregister,
StallGuard und EEPROM-Selbsttest und fährt einzelne Pumpen um X Schritte. „Auch ohne
Treiberantwort takten“ erzeugt STEP-Pulse auch ohne antwortenden TMC2209 (Test mit
Oszilloskop/Logikanalysator).

## Dosierung (M2)

| Thema | Umsetzung |
|---|---|
| Menge → Schritte | Kalibrierung in µl pro Motorumdrehung (unabhängig von den Mikroschritten). Ohne Kalibrierung Nennwert 0,37 ml/U (≈ 111 ml/min bei 300 U/min). |
| Ablauf | Rückzug ausgleichen → Hauptfahrt → Anti-Tropf-Rückzug. Der ausgeführte Rückzug wird spannungsausfallsicher gespeichert. Der Rückzug ist pro Pumpe **ab Werk aus** und lässt sich auf „Standard“ (globaler Wert) oder einen eigenen Wert stellen; beim Ausschalten wird ein offener Rückzug bei der nächsten Dosis noch ausgeglichen. |
| Verbuchung | Beim Start wird die volle Menge auf Tageslimit, Behälter und Gesamtzähler gebucht und gesichert; ein Abbruch schreibt die nicht geförderte Menge gut. Stromausfall während der Fahrt → Menge bleibt gebucht (sichere Seite). |
| Tageslimit | hart pro Pumpe; Fenster Kalendertag oder rollierend 24 h (Engineering). Stunden-Töpfe über 26 h, angebrochene Stunden zählen voll. Ohne gültige Uhrzeit werden Pumpen mit Limit gesperrt. |
| Kalibrierung | Lauf mit Zielmenge (Standard 10 ml), dann gemessene Menge eintragen. Plausibel: 0,25- bis 4-facher Nennwert. Die aufgefangene Menge wird vom Behälter abgezogen. |
| Entlüften | läuft, solange der Button gehalten wird (Heartbeat alle 150 ms, Stopp nach 500 ms ohne Heartbeat oder sofort bei Abbruch der WebSocket-Verbindung, max. 60 s). Zählt nicht für Behälter/Limit, aber für die Schlauch-Laufzeit. |
| Sperren | Betriebsart `disabled`, OTA-Update, Treiberfehler. |
| Speicher | Einstellungen in NVS (`dose`). Laufzustand je Pumpe im EEPROM als A/B-Datensätze (Folgenummer + CRC32, ab 0x000), Rückfall auf NVS ohne EEPROM. |

Engineering-Standardwerte (Super-Admin, global; Rückzug zusätzlich pro Pumpe):
Rückzug „Standard“ ¼ U (≈ 0,09 ml), Kalibrier-Zielmenge 10 ml, Stufen 100/200/300 U/min
(≈ 37/74/111 ml/min), Entlüften max. 60 s / Heartbeat 500 ms, kleinste Dosis 0,02 ml,
Schlauch-Lebensdauer 1000 h, Tageslimit pro Kalendertag. Änderungen brauchen die
Engineering-Freischaltung (siehe [Zugang](#zugang)).

Die Weboberfläche bedient das pro Pumpe; die Engineering-Parameter liegen im
[Engineering-Menü](#engineering-menü-m6).

## Zeitplan (M3)

| Thema | Umsetzung |
|---|---|
| Plan | genau einer pro Pumpe: „verteilt“ („X ml in N Dosen zwischen A und B“, gleichmäßig auf das Raster gelegt, Rundungsrest auf die ersten Dosen, höchstens 96 Dosen pro Tag) oder „Einzelzeiten“ (bis 24 Uhrzeiten, jede mit eigener Menge). Tage und Stromausfall-Verhalten gelten für den ganzen Plan; ob er läuft, bestimmt der Betrieb der Pumpe. |
| Tage | täglich, Wochentage oder „alle N Tage“ (1–30) ab einem Startdatum. |
| Raster | Zeiten auf dem Zeitplan-Raster (Standard 5 min, Engineering). Ortszeit der Geräte-Zeitzone. |
| Zeitumstellung | Zeitzone als POSIX-Regel. Fällt eine Zeit in die Lücke im Frühjahr, wird sie nach hinten verschoben; eine doppelte Stunde im Herbst wird einmal (beim ersten Auftreten) dosiert. |
| Prüfung beim Speichern | alle Pumpen zusammen: Laufzeit jeder Dosis (Menge, Kalibrierung, Geschwindigkeit, Rampe, Rückzug + 2 s) darf die nächste Dosis derselben Pumpe nicht erreichen; Sperrzeiten zwischen Pumpen auch über Mitternacht und für Intervall-Tage; Tageslimit. Verstöße → 409 mit Liste der kollidierenden Dosen, nichts wird gespeichert. |
| Laufzeit-Schutz | Jede Dosierung (auch manuell und Boost) prüft die Sperrzeiten gegen das tatsächliche Ende der letzten Dosis der anderen Pumpe. Eine geplante Dosis wartet dann und wird nach `max_delay_min` (Standard 120 min) verworfen. Entlüften und Kalibrieren sind ausgenommen. |
| Manuelle Dosis in Sperrzeit | Regel `reject` (Standard): 409 mit Zeitpunkt, ab dem es geht. Regel `queue`: vorgemerkt (eine pro Pumpe), startet nach Ende der Sperrzeit. |
| Stromausfall | Bis 2 min Verspätung gilt eine Dosis als pünktlich. Danach wird sie übersprungen (Standard) oder innerhalb eines einstellbaren Fensters nachgeholt. Übersprungene Dosen werden gezählt und mit Grund angezeigt; nach langen Ausfällen wird alles Ältere in einem Schritt übersprungen. |
| Boost | Zusatzmenge über 1–30 Tage, gleichmäßig auf die geplanten Dosen des Tages verteilt; was nicht dosiert werden kann, wandert auf die Folgetage. Endet von selbst. |
| Reichweite | Behälter-Restmenge geteilt durch den Durchschnitt der nächsten 7 Tage (inkl. Boost). |
| Ohne gültige Uhrzeit | Es wird nichts geplant; manuelle Dosen bleiben möglich (außer bei Pumpen mit Tageslimit). |
| Speicher | Pläne, Sperrzeiten, Parameter in NVS (`sched`). Fortschritt je Pumpe (zuletzt bearbeitete Dosis, Boost, Zähler) im EEPROM als A/B-Datensätze ab 0x640, Rückfall auf NVS. Geschrieben wird nur, wenn eine Dosis bearbeitet wurde. |

Zeitplan als JSON (`PUT /api/v1/pumps/schedule?pump=1`; unbekannte Felder werden ignoriert, ein
GET-Ergebnis kann also bearbeitet zurückgeschickt werden):

```json
{"kind": "spread", "start": "08:00", "end": "20:00", "count": 12, "ml": 24,
 "days": {"mode": "daily"}, "power_loss": "skip_missed", "catch_up_min": 60}

{"kind": "times", "times": [{"time": "09:00", "ml": 5}, {"time": "21:30", "ml": 2.5}],
 "days": {"mode": "interval", "every": 2, "anchor": "2026-11-06"}}

{"kind": "none"}
```

Sperrzeiten (`PUT /api/v1/schedule/locks`):

```json
{"manual": "reject", "rules": [{"pumps": [1, 2], "gap_min": 5}]}
```

`"end"` braucht es nur bei mehr als einer Dosis; Einzelzeiten werden beim Speichern sortiert.
Fehler nennen die Stelle (`"entry": 2, "field": "ml"` für
die zweite Uhrzeit in sortierter Reihenfolge) bzw. bei 409 `conflict` die
kollidierenden Dosen (`"conflicts": [{"code": "collision_lock", "first": {…}, "second": {…}, "gap_min": 5}]`).
Zeitpunkte kommen als UTC-Sekunden plus Ortszeit des Geräts (`"at"` + `"local"`), weil der
Browser in einer anderen Zeitzone sein kann.

Zeitpläne, Sperrzeiten und Boost bearbeitet die Weboberfläche (Pumpen-Detail, Einstellungen),
die Scheduler-Parameter das [Engineering-Menü](#engineering-menü-m6).

## Historie (M6)

Das Gerät zeichnet Ereignisse als 24-Byte-Einträge auf der LittleFS-Partition `storage` auf
(eine Datei pro UTC-Tag unter `/storage/hist`, Prüfsumme je Eintrag). Tage älter als die
Aufbewahrungszeit (Engineering-Menü, 1–60 Tage, ab Werk 60, Teil der Datensicherung) löscht es
stündlich, nach dem Verkürzen innerhalb einer Minute. Geschrieben wird über eine Warteschlange in einem eigenen Task – Dosierung
und Zeitplan warten nie auf den Flash. Ohne gültige Uhrzeit wird nichts aufgezeichnet.
Bei rund 240 Dosen am Tag sind das etwa 6 KB pro Tag, 60 Tage ≈ 350 KB von 5,9 MB.
Werkseinstellungen löschen die Historie mit; einzeln löscht sie das Engineering-Menü.

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

## MQTT / Home Assistant (M6)

Ab Werk aus. In den Einstellungen (Profi) Broker eintragen; das Gerät verbindet sich, meldet sich
mit Last Will ab und baut die Verbindung nach Fehlern alle 10 s neu auf. Verschlüsselung:

| Einstellung | Verwendung |
|---|---|
| keine | Port 1883, nur im eigenen Netz |
| TLS, öffentliches Zertifikat | Port 8883, Server-Zertifikat gegen die in der Firmware enthaltene Liste öffentlicher Zertifizierungsstellen geprüft (z. B. Cloud-Broker) |
| TLS, eigenes CA-Zertifikat | Port 8883, gegen ein hochgeladenes CA-Zertifikat (PEM, z. B. vom eigenen Mosquitto mit selbst signiertem Zertifikat) |

Topics unter dem Basis-Topic (Standard: `lg-aquatic-life/<gerätetyp>/<hostname>`, z. B.
`lg-aquatic-life/doser/lgal-doser-e4f5`), Zustände `retained`, Ereignisse nicht:

| Topic | Inhalt |
|---|---|
| `<basis>/status` | `online` / `offline` (Last Will) |
| `<basis>/state` | `{"critical","warning","conditions":["pump1_container_low","rtc_fault",…]}` |
| `<basis>/pump/<n>/state` | `{"activity","total_ml","planned_ml","remaining_ml","days","next","schedule","amount_ml","tube_h","last"}` – bei Änderung, sonst alle 5 min |
| `<basis>/event` | jedes Verlaufs-Ereignis einzeln (QoS 1), Felder wie `GET /api/v1/history` plus `"replay"`; siehe unten |
| `<basis>/pump/<n>/dose/set` | `PRESS` = eingestellte Menge, oder Menge in ml (`2.5`) – nur mit Steuerung |
| `<basis>/pump/<n>/amount/set` | Menge für `PRESS` in ml (im RAM, Start 1 ml) – nur mit Steuerung |
| `<basis>/pump/<n>/schedule/set` | `ON` / `OFF` – Betrieb automatisch / nur manuell (eine ausgeschaltete Pumpe bleibt aus) – nur mit Steuerung |
| `<basis>/stop/set` | beliebig – alle Pumpen stoppen – nur mit Steuerung |
| `<basis>/ota/state` | Fortschritt wie `GET /api/v1/ota/status` (beim Start, alle 5 s, am Ende) |

**Home-Assistant-Erkennung** (Standard an): ein Gerät „LGAL Doser XXXX“ per Device-Discovery
(`homeassistant/device/lgal_doser_xxxx/config`) mit je Pumpe Status, dosiert gesamt
(`total_increasing`, Tageswerte bildet Home Assistant), geplant heute, Vorrat, Reichweite in
Tagen, nächste Dosierung und Schlauch-Betriebsstunden (Diagnose); dazu Alarm und
Warnung des Geräts. Namen folgen der Gerätesprache und den Pumpennamen. Mit **Steuerung**
kommen je Pumpe Zeitplan-Schalter, Dosiermenge und „Dosieren“ sowie „Alle stoppen“ hinzu; ohne
Steuerung entfernt das Gerät diese Entitäten wieder und ignoriert Befehle. Startet Home
Assistant neu (`homeassistant/status` = `online`), sendet das Gerät Discovery und Zustände erneut.
Dosen über MQTT laufen wie manuelle Dosen (Tageslimit, Sperrzeiten, Behälter) und stehen
mit Quelle `mqtt` im Verlauf.

**Ereignisse** (`<basis>/event`, Funktionsbeschreibung 1 und 9): Dosen, ausgelassene Dosen,
Entlüften, Kalibrierung, Behälter, Schlauch und Alarme gehen in der Reihenfolge des Verlaufs
raus, z. B. `{"utc":1794330000,"local":"2026-11-10 18:20:00","kind":"dose","pump":2,"source":"schedule","code":"success","target_ml":1.5,"done_ml":1.5,"replay":false}`.
Das Gerät merkt sich, bis wohin gesendet ist (NVS, höchstens einmal pro Minute geschrieben).
Nach einem Ausfall von Netz oder Broker oder einem Neustart folgen die im Verlauf gepufferten
Ereignisse (bis zur Aufbewahrungszeit) mit `"replay":true`, etwa 80 pro Sekunde; der Zeitplan
läuft davon unberührt weiter. Zustellung mindestens einmal: nach einem Neustart können die
Ereignisse der letzten Minute doppelt kommen (`utc`, `pump`, `kind` erkennen sie). Beim ersten
Start beginnt der Strom mit dem aktuellen Zeitpunkt.

## Datensicherung (M6)

**Zeitplan pro Pumpe** (alle): im Pumpen-Detail als JSON-Datei exportieren und importieren, auch
auf eine andere Pumpe (`{"format":"lgal-doser-schedule","version":1,…,"schedule":{…}}`).

**Ganzes Gerät** (nur Engineering, Bereich E des Engineering-Menüs): eine Datei mit Sprache, Zeitzone,
Zeitserver, LED-Nachtmodus, Netzwerk, allen Pumpen-Einstellungen inkl. Kalibrierung, Zeitplänen,
Sperrzeiten, Servicewerten, Hardware-Parametern (Ströme, Mikroschritte), der Aufbewahrungszeit des Verlaufs und
den MQTT-Einstellungen samt CA-Zertifikat.
Behälterinhalt, Zähler und Verlauf gehören nicht dazu. Beim Export wählbar:

| Variante | Inhalt |
|---|---|
| ohne Passwörter | kein WLAN- und MQTT-Passwort, keine Zugangsdaten; beim Einspielen bleibt das gespeicherte WLAN-Passwort, wenn das Netz (SSID) gleich ist, und das MQTT-Passwort bei gleichem Broker und Benutzer |
| mit Passwörtern | zusätzlich WLAN- und MQTT-Passwort (Klartext) sowie Admin-Passwort und API-Keys (nur als PBKDF2- bzw. SHA-256-Hash, wie im Gerät gespeichert) |

Beim Einspielen zeigt die Seite Gerät, Firmware und Datum der Sicherung und lässt die Teile
wählen; Netzwerk und Zugangsdaten sind vorab abgewählt. Die Firmware prüft das ganze Dokument
(Format, Version, jeden Wert wie die einzelnen Endpunkte) bevor sie etwas übernimmt, und lehnt
es ab, solange eine Pumpe läuft. Zugangsdaten melden alle ab; ein neues Netzwerk gilt 2 s nach
der Antwort. Der Engineering-Schlüssel (Hersteller) ist nie Teil der Sicherung.

## Engineering-Menü (M6)

Verstecktes Experten-Menü für den Hersteller (`#/engineering`, Funktionsbeschreibung 11):
erreichbar über den Engineering-Dialog (7× auf die Firmware-Version im Fuß klicken), jede
Anfrage braucht die Engineering-Freischaltung. Live-Werte fragt die Seite nur ab, solange
jemand Maus oder Tastatur benutzt – so sperrt das Gerät 15 min nach der letzten echten
Eingabe.

| Bereich | Inhalt |
|---|---|
| A · Mechanik | Dosier-Physik für alle Pumpen: Rückzug „Standard“, Kalibrier-Zielmenge, drei Geschwindigkeitsstufen (U/min mit ml/min und Schritten/s). Treiber pro Pumpe: Lauf- und Haltestrom, Mikroschritte, Vollschritte/U, Beschleunigung, Startgeschwindigkeit, Drehrichtung; wahlweise für alle 5 Pumpen. |
| B · Grenzen | Entlüften max. Dauer und Totmann-Timeout, kleinste Einzeldosis, Fenster des Tageslimits, Zeitplan-Raster, max. Verzögerung einer geplanten Dosis. |
| C · Wartung | Schlauch-Lebensdauer, Betriebsstunden pro Pumpe setzen/zurücksetzen. DS3231-Temperatur, Aging-Offset und automatischer Abgleich per NTP (Messung seit, Abweichung jetzt, letzte Korrektur), Hinweis bei leerer Pufferbatterie. Aufbewahrungszeit des Verlaufs (1–60 Tage). |
| D · Diagnose | Raw-Stepper-Test (Schritte vor/zurück, Frequenz, Stopp/Sofort-Stopp, Live-Position und StallGuard; an Behälter, Zeitplan und Verlauf vorbei). Treiberdiagnose: Statusflags (Kurzschluss, Übertemperatur, offene Spule, Stillstand, StealthChop, Stromstufe) und TMC2209-Rohregister. Systemtelemetrie inkl. UART-Statistik und EEPROM-Selbsttest. Gefahrenzone: Verlauf löschen, Netzwerk zurücksetzen (den Werksreset gibt es in der Wartung). |
| E · Datensicherung | Ganzes Gerät sichern und wiederherstellen (siehe [Datensicherung](#datensicherung-m6)). |

Der Werksreset (Wartungsseite oder Taster 10 s) stoppt alle Pumpen, löscht das EEPROM (Laufzustand von
Dosierung und Zeitplan: Zähler, Behälter, Schlauchstunden), NVS und die Datenpartition und
startet neu im Einrichtungsmodus.

## Taster und LED

| Taster (GPIO0) | Aktion beim Loslassen | LED während des Haltens |
|---|---|---|
| ≥ 3 s | Setup-WLAN für 30 min (auch zum Zurücksetzen des Admin-Passworts) | Orange |
| ≥ 10 s | Werkseinstellungen (Pumpen stoppen, EEPROM, NVS und Datenpartition löschen) | Rot |

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
| Gelb, langsam blinkend | Warnung: Behälter fast leer (reicht weniger Tage als die Alarmschwelle, Standard 4), Schlauch-Lebensdauer erreicht, kein Netz, keine gültige Uhrzeit |
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
