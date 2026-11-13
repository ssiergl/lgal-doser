<p align="center"><a href="https://github.com/ssiergl/lgal-brand"><img src="https://raw.githubusercontent.com/ssiergl/lgal-brand/main/logo/lgal-plaque.svg" width="200" alt="LG Aquatic Life"></a></p>

# <img src="components/webui/www/icon.svg" width="36" height="36" align="top" alt=""> LGAL Doser

*[English](README.md) · Deutsch*

Offene Dosierpumpe für Meerwasseraquarien von **LG Aquatic Life**. Das erste Gerät hat fünf
Schlauchpumpen mit Schrittmotor, gesteuert von einem ESP32-S3; Varianten mit weniger Pumpen
sind geplant. Bedient wird sie über eine Weboberfläche im Browser, auf Wunsch zusätzlich über
Home Assistant. Firmware, Platine und Gehäuse liegen offen in diesem Repository.

> **Wichtig:** Eine Dosierpumpe gibt Stoffe ins Aquarium, die in falscher Menge Tiere töten
> können. Vor dem ersten Einsatz die [Sicherheitshinweise](#sicherheit-und-haftung) lesen.

## Was sie kann

- **Fünf Pumpen, unabhängig voneinander:** Kamoer KPHM100-ST mit TMC2209-Treibern. Die Pumpen
  laufen leise im StealthChop-Modus mit sanfter Rampe und können alle gleichzeitig fahren.
- **Genau dosieren:** Kalibrier-Assistent mit Messbecher, Rückzug gegen Nachtropfen,
  Schlauch füllen per Halten (stoppt sofort beim Loslassen oder bei Verbindungsabbruch).
- **Zeitpläne:** pro Pumpe eine Tagesmenge auf N Dosen verteilt oder feste Uhrzeiten mit
  eigener Menge; täglich, an Wochentagen oder alle N Tage. Dazu Boost (Zusatzmenge über
  einige Tage) und Sperrzeiten zwischen Pumpen, z. B. Calcium nicht direkt nach KH.
- **Schutz vor Überdosierung:** hartes Tageslimit pro Pumpe, Behälter-Füllstand mit
  Reichweite in Tagen, Prüfung aller Zeitpläne beim Speichern, festgelegtes Verhalten nach
  einem Stromausfall.
- **Weboberfläche** direkt im Gerät, ohne Cloud und ohne App: Übersicht aller Pumpen,
  Verlauf mit Diagrammen, Ansicht **Einfach** und **Profi**, dunkles und helles Design,
  Deutsch und Englisch.
- **Vernetzt:** LAN als Hauptverbindung, WLAN als Ersatz. Setup-WLAN mit Einrichtungsassistent,
  Uhrzeit per NTP mit Uhrenmodul als Rückfall.
- **Home Assistant / MQTT:** automatische Erkennung, Sensoren je Pumpe, auf Wunsch
  Dosieren und Schalten aus Home Assistant, Update-Entität. TLS mit eigenem CA-Zertifikat.
- **Updates über das Netz** mit automatischem Rollback, falls eine neue Firmware nicht sauber
  startet. Optional prüft das Gerät täglich auf neue Versionen.
- **Status-LED** (rot, gelb, grün …) mit Nachtmodus, ein Taster für Einrichtung und
  Werkseinstellungen.

## Hardware im Überblick

| Baugruppe | Bauteil |
|---|---|
| Steuerung | Waveshare ESP32-S3-DEV-KIT-N16R8 (Firmware auch für N8R8 und N32R8) |
| Pumpen | 5× Kamoer KPHM100-ST, 24 V, Schlauch BPT 3×5 mm |
| Motortreiber | 5× TMC2209 (StepStick), per UART konfiguriert |
| Netzwerk | USR-ES1 (W5500) für LAN, WLAN im ESP32-S3 |
| Uhr | DS3231 mit Pufferbatterie |
| Bedienung | RGB-Statusleuchte, Taster |
| Versorgung | 24 V extern |

Pinbelegung und Schaltung: [CLAUDE.md](CLAUDE.md).

## Dokumentation

| Dokument | Für wen |
|---|---|
| [Bedienungsanleitung](docs/de/benutzer.md) | Anwender: Einrichtung, Pumpen kalibrieren, Zeitpläne, Alltag, Fehlersuche |
| [Nachbau-Anleitung](docs/de/nachbau.md) | Bastler: Teileliste, elektrische Sicherheit, Firmware aufspielen, Aufbau und Test |
| [Technische Dokumentation](docs/de/technik.md) | Entwickler: Firmware bauen, Erstinbetriebnahme per USB, API, MQTT, Interna |
| [Changelog](CHANGELOG.md) (englisch) | was sich von Version zu Version geändert hat |
| [Security](SECURITY.md) (englisch) | Sicherheitsmodell, bekannte Grenzen, Lücken vertraulich melden |

## Projektstand

Die Firmware ist vollständig umgesetzt (Meilensteine M0–M6) und mit Host-Tests, Fuzzern und
einem Geräte-Mock geprüft. Die Platine (v1.0, 11/2026) ist fertig entworfen, der Test der
Firmware auf der echten Hardware steht noch aus. Bis dahin gilt: **noch nicht für den Einsatz
an einem Aquarium mit Tieren geeignet.**

Ohne Gerät lässt sich die Weboberfläche im Browser ausprobieren:

```bash
python3 tools/mock/webui_server.py
```

Danach `http://localhost:8767/` öffnen, Passwort `test-admin-pw`. Mehr dazu in der
[technischen Dokumentation](docs/de/technik.md#mock-server-ohne-gerät).

## Repository

```
components/   Firmware (ESP-IDF-Komponenten) und Weboberfläche
main/         Start der Firmware
tools/        Provisionieren, Update-Upload, Release-Build, Geräte-Mock
tests/        Host-Tests und Fuzzer
docs/         Dokumentation (englisch), docs/de/ deutsche Fassungen
hardware/     Gehäuse (STL …) und Platine (Schaltplan, Gerber …)
```

## Fehler melden

Fehler bitte als Issue mit der Vorlage „Bug report“ eröffnen, gern auch auf Deutsch.
Firmware-Version und, wenn möglich, das Log aus der Wartung angeben – ohne Passwörter oder
Schlüssel.
Sicherheitslücken nicht öffentlich, sondern wie in [SECURITY.md](SECURITY.md) beschrieben.

## Sicherheit und Haftung

**Dosierfehler können Tiere töten.** Eine falsch eingestellte Menge, eine vergessene
Kalibrierung, ein vertauschter Schlauch, ein Softwarefehler oder ein Defekt kann in kurzer
Zeit zu viel oder zu wenig eines Stoffes ins Becken bringen. Deshalb:

- Behälter nur so groß wählen, wie es nötig ist: Was nicht im Behälter ist, kann nicht ins
  Becken gelangen.
- Für jede Pumpe ein **Tageslimit** einstellen, das eine gefährliche Menge sicher ausschließt.
- Nach jeder Änderung (Schlauch, Behälter, Zeitplan, Firmware-Update) kalibrieren bzw.
  kontrollieren und die Wasserwerte in den folgenden Tagen öfter messen.
- Schlauchenden so führen, dass bei Stillstand nichts nachläuft (Rückschlagventil oder
  Schlauch oberhalb des Wasserspiegels).

Das Gerät arbeitet mit 24 V aus einem externen Netzteil. Nur ein geprüftes Netzteil mit
Schutzkleinspannung verwenden, Elektronik vor Spritz- und Salzwasser schützen und das Netzteil
an einer Steckdose mit Fehlerstrom-Schutzschalter (FI) betreiben.

Dieses Projekt wird ohne jede Gewährleistung bereitgestellt (siehe Lizenzen). Nachbau und
Betrieb erfolgen auf eigene Verantwortung; die Autoren haften nicht für Schäden an Tieren,
Aquarium, Einrichtung oder anderen Gegenständen.

## Lizenz

- Firmware und Software: **GPL-3.0-or-later**, siehe [LICENSE](LICENSE) und
  [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt).
- Hardware (Gehäuse, Schaltplan, Platine, Gerber): **CC BY-NC-SA 4.0**, siehe
  [LICENSE-hardware.md](LICENSE-hardware.md). Kommerzielle Nutzung nur mit schriftlicher
  Erlaubnis.
- Name und Logo von LG Aquatic Life: Sie fallen nicht unter diese Lizenzen, siehe
  [TRADEMARKS.md](TRADEMARKS.md) (englisch). Kurz: In unveränderten Kopien, Forks und selbst
  gebauten Geräten darf das Logo bleiben; wer eine geänderte Fassung weitergibt, ersetzt das
  Logo und verwendet einen eigenen Namen.

Copyright © 2026 Lars Greiss / LG Aquatic Life
