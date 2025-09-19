# Agenten-Leitfaden CleverCoffee Silvia

## Kernauftrag
- Ziel: Firmware, Web-Assets und Dokumentation gezielt auf die Rancilio Silvia abstimmen.
- Handle Aufgaben mit Fokus auf thermische Stabilitaet, Nutzerfuehrung und reproduzierbare Builds.
- Nutze diesen Leitfaden als Schnellreferenz; aktualisiere ihn, sobald sich Strukturen oder Defaults aendern.

## Schnelleinstieg
1. PlatformIO Core bereitstellen, ESP32 USB-Treiber sicherstellen.
2. `pio run` fuer Basis-Build, `pio run -t upload` zum Flashen, `pio run -t uploadfs` nach Web-Asset-Aenderungen.
3. OTA: `pio run -t upload -e esp32_ota` (Zugangsdaten liegen in `platformio.ini`).
4. Versionen kommen aus `auto_firmware_version.py`; Datei nicht manuell editieren.

## Architektur auf einen Blick
- `src/` Anwendungslogik (Handler, Tasks, Netzwerk). Kerndateien: `main.cpp`, `brewHandler.h`, `steamHandler.h`.
- `src/display/` OLED-Layouts, Bitmaps, UI-Flows (U8g2 128x64 horizontal/vertikal).
- `src/hardware/` Pin- und Sensor-Abstraktionen inkl. `pinmapping.h`, `TempSensor*.{h,cpp}`.
- `src/utils/` Hilfen fuer Persistenz, Zeit und Parameter (`storage.h`, `SysPara.h`).
- `lib/Logger/` Serieller Logger mit Log-Level-Steuerung.
- `data/` LittleFS-Weboberflaeche; Deployment via `pio run -t uploadfs`.
- `test/` Platzhalter fuer PlatformIO Unit-Tests.
- `platformio.ini` Build-Setup, optional erweiterbar durch `platformio_extra.ini`.

## Hardware-Fokus Rancilio Silvia
- Sensorik: PT100 oder TSic fuer Temperatur, optional Druck-/Gewichtssensoren.
- Heizung ueber Relais, Pumpe via SSR/Dimmer fuer sanfte Preinfusion.
- Display: U8g2-kompatibles OLED 128x64 (Layouts in `src/display/`).
- Firmware-Defaults anpassen in `src/defaults.h` und `src/brewHandler.h` (Thermik, Flow, Profiling).

## Konfiguration & Tuning
- `src/userConfig_sample.h` nach `userConfig.h` kopieren und Silvia-spezifische Werte (Offsets, Pumpenkurven) pflegen.
- PID-Brew-Parameter (`DEFAULT_KP_BREW`, `DEFAULT_KI_BREW`, `DEFAULT_KD_BREW`) dokumentieren und testen.
- Preinfusion- und Brew-by-Weight-Profile in `brewHandler.h` klar kommentieren.
- Secrets (WLAN, OTA) ausschliesslich ueber `platformio_extra.ini` oder dynamische Generierung (z.B. `data/manifest.json`).

## Build- und Release-Workflow
- Standard-Builds gegen `env:esp32_usb` fahren.
- Vor Merge: `pio check --tool clangtidy` fuer statische Analyse.
- Web-UI-Aenderungen immer mit passendem Firmware-Stand testen (LittleFS Upload + Testshot).
- Dokumentiere neue Defaults oder Profile direkt im Commit oder hier im Leitfaden.

## Qualitaetssicherung
- Automatisierte Tests sind aktuell nicht vorhanden; neue Module unter `test/` platzieren.
- Hardware-Aenderungen an echter Silvia pruefen (Aufheizdauer, Temperaturverlauf, Pumpenzyklen, Log-Ausgabe).
- Logger nutzen (`lib/Logger/`) um thermische oder Pumpen-Anomalien nachvollziehbar zu machen.

## Typische Agentenaufgaben
- Silvia-Parameter und PID-Defaults pflegen.
- Display-Layouts oder Web-UI-Elemente erweitern.
- Sensor- und Pin-Mappings fuer Varianten dokumentieren und implementieren.
- MQTT-Topics, API- und Build-Skripte (`auto_firmware_version.py`) an neue Anforderungen anpassen.

## Wissensquellen
- Hauptdokumentation: `README.md`
- Beitragsrichtlinien: `CONTRIBUTING.md`
- Handbuch (de): https://rancilio-pid.github.io/ranciliopid-handbook/
- Community: Discord-Link im `README.md`

> Halte den Leitfaden synchron, sobald neue Hardwareprofile, Buildziele oder Arbeitsablaeufe hinzukommen.
