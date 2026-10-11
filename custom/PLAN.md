# NSPanel Custom UI – Plan

Ziel: eigene, schnelle Oberfläche für das NSPanel EU (Bad/Flur), komplett von Claude gestaltet.
Basis: Fork von NSPanel Easy. Eigene Firmware unter `custom/` und `components/nspanel_ui/`.

## Architektur

- **Display (Nextion-TFT):** nur Ressourcen – vorgerenderte Bilder (Seiten, Zustände, Thermostat-Ring, Wetter-Icons)
  und die geglätteten Sarasa-Schriften aus `hmi/dev/fonts`. Eine einzige Nextion-Seite ohne Komponenten.
  Die Seite meldet jede Berührung mit Koordinaten an den ESP32 (`0x65`-Event, x/2 und y/2).
- **ESP32 (ESPHome):** die gesamte Logik. Zeichnet mit `pic`/`xpic`/`xstr`/`fill`, wertet Touches selbst aus
  (Tippen, Halten, Wischen), holt Zustände direkt aus Home Assistant, ruft HA-Aktionen auf.
  Kein AppDaemon, kein Blueprint, keine MQTT-Rundreise → schnell.
- **Sichtbarer Bereich:** EU-Modell zeigt nur x = 0…449 (rechte 30 px verdeckt).
- **Kompatibilität:** gleiche Gerätenamen, Entitäten und API-Aktionen wie bisher
  (`esphome.nspanel_notify_fullscreen`, `_wake`, `_dim_0_to_100`, `_play_rtttl`, `_notify_on_screensaver`, …),
  MQTT entfällt („On all off“ hört jetzt auf `switch.nspanel_relay_2` aus der ESPHome-API).

## Schritte

1. [x] Design final für 450×320, nur flache Flächen unter dynamischem Text (`custom/design/ui.html`)
2. [x] Asset-Pipeline `custom/tools/build_assets.py`: Bilder rendern (Edge headless), Koordinaten exportieren,
       Thermostat-Ringe, Wetter-Icons, Schriftbreiten aus `.zi` → `components/nspanel_ui/generated_*.h`
3. [x] Nextion-Bausatz `custom/nextion/`: Bilder 000–038, Schriften 0–6, Program.s, Seiten-Events, Anleitung
4. [x] **Nutzer:** TFT im Nextion Editor kompilieren (Anleitung `custom/nextion/BUILD.md`, ca. 10 Minuten)
5. [x] ESPHome-Komponente `components/nspanel_ui` (Seiten, Zeichnen, Touch, Bildschirmschoner, Overlays)
6. [x] ESPHome-Paket `custom/nspanel_custom.yaml` (Hardware, HA-Zustände, Aktionen, Kompatibilitäts-API)
7. [x] Kompilieren über den ESPHome-Builder in HA, Fehler beheben (Testkonfiguration `nspanel-ui-test.yaml`,
       RAM 31 %, Flash 55 %)
8. [x] Installation: TFT nach `/local/nspanel/` hochladen, TFT aufs Panel, Firmware per OTA, HA-Aktionen erlauben
9. [x] AppDaemon gestoppt, MQTT entfernt, Doku (`custom/README.md`) – Praxistest am Panel läuft
