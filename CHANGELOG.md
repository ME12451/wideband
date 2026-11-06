# Changelog — Wideband-Firmware

Die Daten bezeichnen die Veröffentlichung der vorhandenen Entwicklungsstände in dieser Fork. Binärdateien und Quellstände sind in den verlinkten Releases festgehalten.

## Manager F0 0.5.3 — veröffentlicht am 2026-11-06

Branch: `manager-latest` · importierter Firmwarestand: `791684b`

Enthält die Selectable-Bitratenwahl und zusätzlich:

- Software-Rtrim für Bosch LSU 4.9 mit dem auf der eigenen Sonde angegebenen Widerstandswert; Auswahl zwischen physischem Trim und Softwarekorrektur.
- Erweiterter CAN-Dienst zum Lesen und Schreiben von Konfiguration und Rtrim, für Identifikation, Neustart, Werkseinstellungen und Diagnoseschnappschüsse.
- Konfiguration über **Hardware-ID plus aktuellen Datenindex**. Zwei Module mit gleicher Hardware-ID lassen sich bei unterschiedlichen Indizes getrennt konfigurieren.
- Antworten auf Indexänderungen verwenden die alte Adresse; anschließendes Rücklesen erfolgt an der neuen Adresse.
- Das ältere Konfigurationsprotokoll bleibt in der Firmware erhalten. Ältere Werkzeuge adressieren weiterhin nur die Hardware-ID und benötigen eindeutige Hardware-IDs.
- Der Bootloader bleibt gegenüber Selectable unverändert. Firmwareupdates über CAN benötigen weiterhin genau ein angeschlossenes Modul.

[Release mit originalen Anwendungs-, Bootloader- und Vollimages](https://github.com/ME12451/wideband/releases/tag/manager-f0-0.5.3). Anwendungs-SHA256: `115fdf2327c34ee9c6c4a42efd5959679e707484614573c498f71ea976b8750c`.

Die Veröffentlichung übernimmt den bereits gebauten Stand 0.5.3. Der spätere Linux-Manager-Port ändert die Firmware nicht. Beide Manager-Plattformen verwenden diese indexbasierte Firmware für die erweiterten Konfigurationsfunktionen.

## Selectable F0 — veröffentlicht am 2026-11-06

Branch: `wideband-selectable` · ursprünglicher Implementierungscommit: `64cdf9f`

- CAN-Bitrate dauerhaft zwischen 500 kbit/s und 1 Mbit/s wählbar.
- Anwendung und CAN-Bootloader verwenden denselben gespeicherten Wert.
- Eine Änderung wird beim nächsten Modulreset aktiv; ohne gültigen Eintrag gilt 500 kbit/s.
- Bestehende Einstellungen bleiben bei Konfigurationsänderungen erhalten; gespeicherte Datensätze werden geprüft.
- Erstinstallation benötigt den passenden Bootloader über SWD. Eine reine CAN-Anwendungsaktualisierung ersetzt den Bootloader nicht.
- Hosttests für Bitratenwahl, beschädigte und unterbrochene Datensätze, Schreibfehler und Antwortzuordnung ergänzt.

Dies ist der minimale Eingriff für die Bitratenwahl. Der erweiterte Manager-Konfigurationsdienst ist hier nicht enthalten.

[Release mit Firmware, Dokumentation und Prüfsumme](https://github.com/ME12451/wideband/releases/tag/selectable-f0-2026-10-25). Das Paket wurde ursprünglich am 2026-10-25 gebaut.

## Grundlage und Struktur

Fork von `rusefi/wideband` mit ursprünglicher Git-Historie, Verzeichnisstruktur und Submodulen. Die eigenen Änderungen liegen auf separaten Branches; bei ihrer Veröffentlichung wurde `master` nicht verändert. Der Windows-/Linux-Manager liegt in einem [separaten Repository](https://github.com/ME12451/rusefi-wideband-manager).

## Validierungsgrenzen

Firmware wurde gebaut und mit Hosttests geprüft. Keine Tests mit physischen CAN-Adaptern oder Wideband-Modulen und keine reale Flashprüfung. Konfigurationsschreiben und Firmwareupdates zuerst auf dem Prüfstand prüfen.
