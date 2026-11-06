# Changelog — Wideband-Firmware

Die Daten bezeichnen die Veröffentlichung der vorhandenen Entwicklungsstände in dieser Fork. Binärdateien und Quellstände sind in den verlinkten Releases festgehalten.

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
