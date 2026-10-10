# NSPanel Custom UI

Eigene, schnelle Oberfläche für das Sonoff NSPanel EU. Das Display (Nextion) enthält nur vorgerenderte Bilder und
Schriften. Alles andere erledigt der ESP32: zeichnen, Touch auswerten, Zustände aus Home Assistant holen,
Aktionen ausführen.

## Bedienung

| Geste | Wirkung |
| --- | --- |
| Tippen | Licht an/aus, Szene, Preset, Medien-Taste, Navigation unten |
| Wischen links/rechts | nächste/vorige Seite (Licht → Licht 2 → Szenen → Bad …) |
| 2 s halten | Wohnungstür aufschließen, Alarm schärfen (Fortschrittsbalken) |
| Tippen auf die Punkte oben | nächste Licht-Seite |
| Erstes Tippen im Bildschirmschoner | nur aufwecken |

Seiten: **Start** (Uhr, Wetter, Bad, Schnell-Szenen) · **Licht** (2 Seiten + Szenen) · **Bad** (Thermostat, Fenster,
Presets) · **Medien** (LG-TV) · **Wohnung** (Schloss, TV-Schnellwahl: Lautstärke, Stumm, Netflix/Live TV/HDMI 2, Alarm, Fenster).

Nach 30 s ohne Berührung erscheint der Bildschirmschoner (Uhr, Datum, Wetter, Bad-Werte).

## Home Assistant

Entitäten des Panels: `number.nspanel_display_brightness`, `number.nspanel_screensaver_brightness`,
`button.nspanel_update_tft`, `button.nspanel_restart`, Relais, Temperatur.

TV-Quellen der Wohnung-Seite: `tv_source_1..3` in den `substitutions` (Namen wie in der Quellenliste des TVs).

Die API-Aktionen der alten Firmware bleiben gleich, bestehende Automationen funktionieren weiter:
`esphome.nspanel_notify_fullscreen`, `_notify_on_screensaver`, `_wake`, `_dim_0_to_100`, `_play_rtttl`,
`_show_screensaver`, `_navigate_to_page` (`home`, `licht`, `licht2`, `szenen`, `bad`, `medien`, `wohnung`),
`_enable_screensaver`, `_disable_screensaver`, `_upload_tft`, `_send_command`.

Ein Tipp auf einen Knopf im Hinweis-Fenster (`notify_fullscreen`) löst das Event `esphome.nspanel_ui` mit
`type: notification_button` und `value: <Knopftext>` aus.

Voraussetzung: In der ESPHome-Integration des Panels ist **„Gerät darf Home-Assistant-Aktionen ausführen“** aktiv.

## Anpassen

- **Andere Entitäten:** in der Gerätekonfiguration (`nspanel.yaml` im ESPHome-Builder) unter `substitutions`
  überschreiben, z. B. `light_7: light.kueche_insel` oder `scene_4: scene.bad_entspannen`.
  Die Namen auf den Kacheln sind Teil der Bilder → für neue Namen das Design anpassen (siehe unten).
- **Design ändern:** `custom/design/ui.html` bearbeiten, dann

  ```bash
  python custom/tools/build_assets.py
  ```

  Das erzeugt die Bilder (`custom/nextion/pictures`), Schriften und die Koordinaten für die Firmware.
  Danach die TFT neu bauen (`custom/nextion/BUILD.md`), nach `/homeassistant/www/nspanel/` hochladen und
  am Panel **Update TFT** drücken.
- **Firmware-Logik:** `components/nspanel_ui/nspanel_ui.cpp`, Paket: `custom/tools/gen_package.py`
  (erzeugt `custom/nspanel_custom.yaml`).

## Zurück zur alten Firmware

Die alte Gerätekonfiguration (Lovelace UI) ist gesichert. Zurück: alte YAML im ESPHome-Builder einspielen,
installieren, dann mit `esphome.nspanel_upload_tft` die Lovelace-TFT wieder aufspielen und AppDaemon starten.
