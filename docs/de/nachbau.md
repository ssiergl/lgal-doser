# LGAL-Doser – Nachbau-Anleitung

*[English](../build-guide.md) · Deutsch*

Diese Anleitung beschreibt, wie man die Dosierpumpe selbst aufbaut: Teile, elektrische
Sicherheit, Firmware aufspielen, Inbetriebnahme und Test. Sie richtet sich an Bastler, die
löten, mit einem Multimeter messen und ein Python-Skript auf der Kommandozeile starten können.

> **Stand:** Platine v1.0 (11/2026) ist entworfen, Firmware fertig. Der Test der Firmware auf
> der echten Hardware steht noch aus, Platinen- und Gehäusedaten folgen danach in
> [`hardware/`](../../hardware/). Werte, die erst mit der Hardware-Freigabe feststehen, sind unten
> als **offen** markiert.

> **Dosierfehler können Tiere töten.** Ein selbst gebautes Gerät erst ausgiebig ohne Tiere
> testen (Abschnitt [Probelauf](#8-probelauf-ohne-tiere)). Nachbau und Betrieb auf eigene
> Verantwortung, siehe [Sicherheit und Haftung](../../README.de.md#sicherheit-und-haftung).

## Inhalt

1. [Teileliste](#1-teileliste)
2. [Werkzeug](#2-werkzeug)
3. [Elektrische Sicherheit](#3-elektrische-sicherheit)
4. [Platine aufbauen und prüfen](#4-platine-aufbauen-und-prüfen)
5. [Firmware aufspielen und provisionieren](#5-firmware-aufspielen-und-provisionieren)
6. [Zusammenbau](#6-zusammenbau)
7. [Inbetriebnahme und Test](#7-inbetriebnahme-und-test)
8. [Probelauf ohne Tiere](#8-probelauf-ohne-tiere)

## 1. Teileliste

| Pos. | Anzahl | Teil | Hinweis |
|---|---|---|---|
| 1 | 1 | Platine LGAL-Doser v1.0 | Fertigungsdaten in [`hardware/platine/`](../../hardware/pcb/) (folgen) |
| 2 | 1 | Waveshare ESP32-S3-DEV-KIT-N16R8 | 16 MB Flash, 8 MB PSRAM; gesteckt auf Buchsenleisten. Zu den Varianten N8R8 und N32R8 siehe [technische Doku](technik.md#modulvarianten) |
| 3 | 5 | Kamoer KPHM100-ST | Schlauchpumpe mit Schrittmotor, 24 V, 1,0 A, 200 Vollschritte/U, ohne Getriebe |
| 4 | 5 | TMC2209-StepStick (z. B. Ulegqin V2.0) | mit Kühlkörper; Sense-Widerstand prüfen (siehe [unten](#tmc2209-treiber)) |
| 5 | 1 | USR-ES1 (W5500) | Ethernet über SPI |
| 6 | 1 | DS3231-Modul mit AT24C32-EEPROM | Uhr (I²C 0x68) und Speicher (0x57) |
| 7 | 1 | Pufferbatterie für das DS3231-Modul | LIR2032 (Akku) oder CR2032 – eine CR2032 **nur ohne Ladediode**, siehe [Sicherheit](#3-elektrische-sicherheit) |
| 8 | 1 | RGB-Leuchte Mentor 2660.8302 | gemeinsame Anode |
| 9 | 1 | Taster Multimec 3F | Einrichtung und Werkseinstellungen |
| 10 | 1 | Schaltregler RECOM R-78E5.0 | 24 V → 5 V für das ESP-Board |
| 11 | 1 | Schaltregler RECOM R-78E3.3 | 24 V → 3,3 V für die Logik |
| 12 | 5 | JST-XH-Steckverbinder 4-polig (Buchse + Stecker) | Motoranschluss |
| 13 | 5 | M8-Einbaubuchse 3-polig | reserviert für spätere Erweiterungen (3,3 V / Signal / GND), von der Firmware nicht genutzt |
| 14 | 1 | Netzteil 24 V DC, Schutzkleinspannung (SELV) | Leistung und Steckverbinder **offen** (folgen mit der Platinen-Stückliste) |
| 15 | 1 | Sicherung in der 24-V-Zuleitung | Wert und Bauform **offen** (folgen mit der Platinen-Stückliste) |
| 16 | 1 | Gehäuse | Druckdaten in [`hardware/gehaeuse/`](../../hardware/enclosure/) (folgen) |
| 17 | – | Pumpenschlauch BPT 3×5 mm als Ersatz, Dosierschläuche, Rückschlagventile, Halter am Becken | Rückschlagventil oder Schlauchende über dem Wasserspiegel |
| 18 | – | Passive Bauteile, Transistoren, Buchsenleisten | laut Stückliste der Platine (1 kΩ für die Treiber-UART, 10-kΩ- und 4,7-kΩ-Pull-ups, 100 nF am Taster, NPN-Transistoren für die LED) |
| 19 | 1 | Aufkleber für das Typenschild | Geräte-Passwort und Name des Setup-WLANs |

Pinbelegung und Schaltung sind festgelegt, siehe [CLAUDE.md](../../CLAUDE.md).

## 2. Werkzeug

- Lötstation, Lötzinn, Entlötlitze
- Multimeter (Spannung, Durchgang)
- USB-Kabel für das ESP-Board und ein Rechner mit Python 3 (für `esptool` und die Skripte);
  zum Selbstbauen der Firmware ESP-IDF v6.0.3 (siehe [technische Doku](technik.md#toolchain))
- Messbecher oder Spritze mit 0,1-ml-Teilung zum Kalibrieren
- 3D-Drucker für das Gehäuse (oder ein eigenes Gehäuse)

## 3. Elektrische Sicherheit

Die Dosierpumpe selbst arbeitet nur mit **24 V Gleichspannung**. Gefährlich ist die
Kombination aus Netzteil, Wasser und Salz:

- **Netzteil:** nur ein fertiges, geprüftes Netzteil mit CE-Kennzeichnung und
  Schutzkleinspannung (SELV) verwenden, kurzschluss- und überlastfest. Die Netzseite
  (230 V) nie selbst bauen oder öffnen.
- **Absicherung:** Zusätzlich zum Schutz im Netzteil gehört eine Sicherung in die 24-V-Zuleitung
  des Geräts, damit ein Fehler auf der Platine (z. B. ein defekter Treiber) keinen Brand
  auslösen kann. Wert und Bauform legt die Platinen-Stückliste fest (**offen**). Leitungen
  passend zum Strom bemessen.
- **Fehlerstrom-Schutzschalter:** Das Netzteil an einer Steckdose mit FI (RCD, 30 mA)
  betreiben – am Aquarium ohnehin Pflicht für alle Geräte.
- **Wasser:** Gerät und Netzteil oberhalb des Wasserspiegels und außerhalb von Spritzwasser
  aufstellen, Kabel mit Tropfschlaufe verlegen, Salzkriechen beachten. Keine Schläuche über das
  Gerät führen.
- **Motoren nie unter Spannung an- oder abstecken.** Ein TMC2209 kann dabei sofort
  zerstört werden. Vorher das Netzteil ziehen.
- **StepSticks richtig herum** einsetzen (Beschriftung GND/VM bzw. EN beachten); falsch
  herum zerstört sie und kann die Platine beschädigen.
- **Pufferbatterie:** Viele DS3231-Module laden die Batterie über eine Diode und sind für einen
  LIR2032-Akku gedacht. Eine nicht wiederaufladbare **CR2032 darf nur ohne diese Ladediode**
  eingesetzt werden, sonst kann sie auslaufen oder platzen.
- **Erst messen, dann bestücken:** siehe nächster Abschnitt.

## 4. Platine aufbauen und prüfen

1. Platine nach Stückliste und Bestückungsplan bestücken (folgen in
   [`hardware/platine/`](../../hardware/pcb/)). ESP-Board, StepSticks, Ethernet- und
   RTC-Modul noch **nicht** einstecken.
2. **Sichtprüfung:** Lötbrücken, kalte Lötstellen, Polung der Bauteile.
3. **Ohne Spannung messen:** kein Kurzschluss zwischen 24 V und GND, 5 V und GND, 3,3 V und GND.
4. **Erster Strom ohne Module:** 24 V anlegen (am besten über ein Labornetzteil mit
   Strombegrenzung) und 5 V sowie 3,3 V an den Buchsenleisten nachmessen.
5. Spannung wieder trennen.

### TMC2209-Treiber

- Die Treiber werden ausschließlich per UART konfiguriert. Ein Poti oder VREF muss nicht
  eingestellt werden, Jumper für Mikroschritte gibt es nicht: Die Adressen 0–3 (Bus A) und 0
  (Bus B) sind auf der Platine fest verdrahtet.
- **Sense-Widerstand prüfen:** Die Firmware rechnet die Motorströme mit 0,11 Ω (bei diesen
  Modulen üblich, Aufdruck meist `R110`). Steht dort ein anderer Wert, stimmen die Ströme nicht
  – dann vor dem ersten Motorlauf ein Issue eröffnen.
- Kühlkörper aufkleben.

## 5. Firmware aufspielen und provisionieren

Das ESP-Board ist im fertigen Gerät nicht per USB erreichbar. Die erste Firmware wird deshalb
**vor dem Einstecken** in die Platine aufgespielt, danach laufen alle Updates über das Netz.

Jedes Gerät braucht ein eigenes Geheimnis, aus dem es Geräte-Passwort und
Engineering-Schlüssel ableitet. Dafür gibt es zwei Wege:

**A – mit eigenem Hersteller-Master (empfohlen, wenn man mehrere Geräte baut):**

1. Einmalig eine Datei `factory.secret` mit einer langen Zufallszeile im Projektordner anlegen
   und sicher aufbewahren:

   ```bash
   python3 -c "import secrets; print(secrets.token_hex(32))" > factory.secret
   ```

2. Firmware flashen – aus einem Release die Gesamtdatei passend zu Board und Modul
   (`d5v1` = LGAL-Doser 5, Platine v1.0):

   ```bash
   python -m esptool --chip esp32s3 write-flash 0x0 lgal_doser-d5v1-N16R8-full.bin
   ```

   oder selbst gebaut mit `idf.py -p /dev/cu.usbmodem* flash`.

3. Provisionieren (schreibt das Geräte-Geheimnis und zeigt die Werte fürs Typenschild):

   ```bash
   python3 tools/provision.py -p /dev/cu.usbmodem*
   ```

   Sind weniger Pumpen eingebaut, z. B. `--pumps 1,2,3` anhängen: Das Gerät startet dann mit
   genau diesen Pumpen, auch nach einem Werksreset.

   Die Werte lassen sich später jederzeit aus der MAC-Adresse neu berechnen
   (`tools/ap_password.py <MAC> --engineering`).

**B – ohne Master (Einzelstück):** Beim ersten Start erzeugt das Gerät selbst ein zufälliges
Geheimnis und zeigt **Geräte-Passwort und Engineering-Schlüssel ein einziges Mal** auf der
seriellen Konsole. Damit diese Ausgabe nicht verloren geht, nach dem Flashen nicht sofort
starten, sondern erst den Monitor öffnen:

```bash
python -m esptool --chip esp32s3 --after no-reset write-flash 0x0 lgal_doser-d5v1-N16R8-full.bin
```

```bash
idf.py -p /dev/cu.usbmodem* monitor
```

Der Monitor startet das Board neu, die Werte erscheinen beim ersten Start. Beide sofort
notieren: Niemand kann sie neu berechnen. Provisionieren nach Weg A ersetzt sie später.

Danach: **Typenschild** mit Geräte-Passwort und Setup-WLAN-Name (`LG-Aquatic-Life-Setup-XXXX`)
beschriften und ins Gehäuse kleben. Den Engineering-Schlüssel getrennt aufbewahren, er gehört
nicht aufs Typenschild.

Einzelheiten und Hintergründe: [technische Doku, Erstinbetriebnahme](technik.md#erstinbetriebnahme-einmalig-per-usb-vor-dem-einbau).

## 6. Zusammenbau

1. Spannung aus. ESP-Board, StepSticks (richtig herum, mit Kühlkörper), Ethernet- und RTC-Modul
   (mit Batterie) einstecken.
2. Platine, Pumpen, Taster und LED ins Gehäuse setzen.
3. **Pumpen anschließen:** Pumpe 1 ist von vorne gesehen die linke, die Nummern auf Gehäuse und
   Platine müssen übereinstimmen.
4. Typenschild einkleben, LAN-Kabel einstecken, zuletzt das Netzteil.

## 7. Inbetriebnahme und Test

1. **Start:** Die LED blendet weiß auf und pulsiert dann orange (Setup-WLAN an).
2. **Einrichten** wie in der [Bedienungsanleitung](benutzer.md#3-ersteinrichtung) beschrieben.
3. **Engineering-Menü öffnen:** in der Oberfläche 7× schnell auf die Firmware-Version im Fuß
   klicken und den Engineering-Schlüssel eingeben.
4. **Bestückung** (Bereich A): Sind weniger als fünf Pumpen eingebaut und wurde ohne `--pumps`
   provisioniert, die fehlenden abwählen. Das Gerät startet danach neu.
5. **Treiber prüfen** (Bereich D): Alle bestückten Treiber müssen antworten, ohne Fehlerflags
   (Kurzschluss, Übertemperatur, offene Spule).
6. **Pumpen einzeln fahren** (Bereich D, Raw-Test): ein paar Umdrehungen vorwärts. Läuft eine
   Pumpe falsch herum, in der Ansicht Profi unter Pumpe → Einstellungen die **Drehrichtung**
   umstellen und mit „Schlauch füllen“ prüfen.
7. **Uhr:** Einstellungen → Uhrzeit zeigt Quelle „Zeitserver“ und das Uhrenmodul ohne Warnung.
8. **Alle Pumpen kalibrieren**, siehe [Bedienungsanleitung](benutzer.md#64-kalibrieren).

## 8. Probelauf ohne Tiere

Bevor das Gerät an ein Becken mit Tieren kommt:

1. Behälter mit Wasser, Schlauchenden in Messbecher.
2. Für jede Pumpe einen Zeitplan und ein Tageslimit einstellen und mindestens 24 Stunden laufen
   lassen.
3. Die geförderten Mengen nachmessen und mit dem Verlauf vergleichen; Abweichungen über einige
   Prozent → neu kalibrieren, Schlauch prüfen.
4. Einen Stromausfall simulieren (Netzteil ziehen, nach einigen Minuten wieder einstecken) und
   prüfen, dass das Gerät sich wie eingestellt verhält (verpasste Dosen auslassen oder
   nachholen) und die Uhrzeit stimmt.
5. Erst dann mit den echten Lösungen und anfangs reduzierten Mengen starten, die Wasserwerte in
   den ersten Tagen öfter messen.

Fehler beim Nachbau oder in der Firmware bitte als Issue melden; Sicherheitslücken nicht
öffentlich, sondern wie in [SECURITY.md](../../SECURITY.md) beschrieben.
