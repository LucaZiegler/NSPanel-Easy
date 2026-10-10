# TFT bauen (Nextion Editor, ca. 10 Minuten)

Das Display bekommt nur Bilder und Schriften, die ganze Logik läuft im ESP32.
Deshalb ist das Projekt bewusst minimal: eine Seite, keine Komponenten.

Alle Dateien liegen im Ordner `custom/nextion/` dieses Repos.

## 0. Nextion Editor installieren

Download: <https://nextion.tech/nextion-editor/> (aktuelle Version, Windows).

## 1. Basisprojekt öffnen

1. **File → Open** → `hmi/nspanel_blank.hmi` (aus diesem Repo).
   Das Projekt hat schon das richtige Display-Modell und die richtige Ausrichtung für das NSPanel EU.
2. **File → Save As** → `custom/nextion/nspanel_custom.HMI`.

## 2. Seite leeren

1. Links in der Seitenliste die Seite **blank** anklicken.
2. Alle Elemente auf der Seite löschen: in die Seite klicken, **Strg+A**, **Entf**.
   Auch die Variablen und Timer unterhalb der Seite (`counter`, `aux1`, `va0`, `va1`, `csv`, `version`, `tm0`, `tm_esphome`)
   anklicken und löschen.
3. Rechts bei den Seiten-Attributen: **sta** = `solid color`, **bco** = `0`.

## 3. Code einfügen

Unten im Event-Bereich der Seite **blank**:

| Reiter | Inhalt |
| --- | --- |
| Preinitialize Event | alles löschen (leer lassen) |
| Postinitialize Event | leer |
| Touch Press Event | Inhalt von `page0_touch_press.txt` |
| Touch Release Event | Inhalt von `page0_touch_release.txt` |
| Page Exit Event | leer |

Dann links **Program.s** öffnen, alles löschen und den Inhalt von `Program.s.txt` einfügen.

## 4. Schriften ersetzen

1. Unten links den Reiter **Fonts** öffnen.
2. Alle vorhandenen Schriften löschen (erste anklicken, Umschalt + letzte anklicken, Entf).
3. **+** → im Ordner `custom/nextion/fonts` die Dateien **einzeln in dieser Reihenfolge** hinzufügen:
   `0_…`, `1_…`, `2_…`, `3_…`, `4_…`, `5_…`, `6_…`
4. Kontrolle: die Schrift-IDs 0–6 passen zu den Dateinamen (0 = Sarasa 16 … 6 = Sarasa 112 time).

## 5. Bilder ersetzen

1. Reiter **Pictures** öffnen, alle vorhandenen Bilder löschen.
2. **+** → im Ordner `custom/nextion/pictures` alle 39 Bilder markieren (Strg+A) → Öffnen.
3. **Kontrolle (wichtig):** Bild-ID **0** muss `000_home_base` sein, ID **16** das Wetter-Sprite und ID **38** `038_ring_off`.
   Stimmt die Reihenfolge nicht, alle wieder löschen und die Bilder in zwei, drei Blöcken nacheinander hinzufügen
   (erst 000–016, dann 017–038).

## 6. Kompilieren und exportieren

1. **Compile** (F5) – unten muss `0 Error(s)` stehen.
2. **File → TFT file output** → speichern als `custom/nextion/nspanel_custom.tft`.

Fertig. Den Rest (Hochladen aufs Panel, Firmware) übernimmt Claude.
