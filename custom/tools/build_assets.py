#!/usr/bin/env python3
"""Build all assets for the NSPanel custom UI from custom/design/ui.html.

Outputs
  custom/nextion/pictures/NNN_<name>.png   opaque pictures in Nextion import order (id = NNN)
  custom/nextion/fonts/N_<name>.zi         fonts in Nextion import order (id = N)
  components/nspanel_ui/generated_layout.h picture ids, rectangles, colours (from the HTML layout export)
  components/nspanel_ui/generated_fonts.h  glyph advance widths for text wrapping / ellipsis
  custom/build/preview/*.png               full-size previews

Requires: Microsoft Edge or Chrome (headless), Python 3.10+, Pillow.
"""
from __future__ import annotations

import atexit
import base64
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
CUSTOM = ROOT / "custom"
HTML = CUSTOM / "design" / "ui.html"
PICS = CUSTOM / "nextion" / "pictures"
FONTS = CUSTOM / "nextion" / "fonts"
PREVIEW = CUSTOM / "build" / "preview"
COMPONENT = ROOT / "components" / "nspanel_ui"
FONT_SRC = ROOT / "hmi" / "dev" / "fonts"

BROWSERS = [
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    "/usr/bin/chromium",
    "/usr/bin/google-chrome",
]

# Full-screen pictures: (name, page, variant). Order defines the Nextion picture id.
SCREENS = [
    ("home_base", "home", "base"),
    ("home_alt", "home", "alt"),
    ("licht_off", "licht", "off"),
    ("licht_on", "licht", "on"),
    ("licht2_off", "licht2", "off"),
    ("licht2_on", "licht2", "on"),
    ("szenen", "szenen", "base"),
    ("bad_base", "bad", "base"),
    ("bad_on", "bad", "on"),
    ("medien_base", "medien", "base"),
    ("medien_on", "medien", "on"),
    ("wohnung_a", "wohnung", "a"),
    ("wohnung_b", "wohnung", "b"),
    ("wohnung_c", "wohnung", "c"),
    ("modal_two", "modal", "two"),
    ("modal_one", "modal", "one"),
]
LAYOUT_PAGES = ["home", "licht", "licht2", "szenen", "bad", "medien", "wohnung", "screensaver", "modal"]
WX_ICONS = ["sunny", "night", "partly", "cloudy", "rainy", "pouring", "snowy", "fog", "lightning", "windy", "hail", "unknown"]
RING_MIN, RING_MAX, RING_STEP = 16.0, 26.0, 0.5
THERMO_RECT = (12, 44, 238, 212)

# Nextion font id -> source .zi file
FONT_FILES = [
    "0. Sarasa 16 full.zi",
    "1. Sarasa 18 full.zi",
    "2. Sarasa 22 full.zi",
    "4. Sarasa 36 full.zi",
    "5. Sarasa 48 limited.zi",
    "6. Sarasa 72 limited.zi",
    "11. Sarasa 112 time.zi",
]
WIDTH_TABLE_FIRST, WIDTH_TABLE_LAST = 0x20, 0x17F
EXTRA_CPS = [0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x201E, 0x2022, 0x2026, 0x20AC, 0x25CF]


def browser() -> str:
    for b in BROWSERS:
        if Path(b).exists():
            return b
    sys.exit("No Edge/Chrome found")


def url(**params) -> str:
    q = "&".join(f"{k}={v}" for k, v in params.items())
    return HTML.resolve().as_uri() + ("?" + q if q else "")


class Browser:
    """Headless Edge/Chrome driven over the DevTools protocol (one instance for the whole build)."""

    PORT = 9337

    def __enter__(self):
        # Profile outside the repo: a user-data-dir path containing spaces makes Edge exit silently.
        self.profile = tempfile.mkdtemp(prefix="nspanel-edge-")
        atexit.register(shutil.rmtree, self.profile, True)
        self.proc = subprocess.Popen(
            [browser(), "--headless=new", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
             f"--remote-debugging-port={self.PORT}", f"--user-data-dir={self.profile}", "--no-first-run",
             "--window-size=480,320", "about:blank"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(100):
            try:
                with urllib.request.urlopen(f"http://127.0.0.1:{self.PORT}/json/list", timeout=1) as r:
                    targets = [t for t in json.load(r) if t.get("type") == "page"]
                if targets:
                    break
            except OSError:
                pass
            time.sleep(0.2)
        else:
            sys.exit("browser did not start")
        self.ws = websocket_connect(targets[0]["webSocketDebuggerUrl"])
        self.seq = 0
        self.call("Page.enable")
        self.call("Emulation.setDeviceMetricsOverride", width=480, height=320, deviceScaleFactor=1, mobile=False)
        return self

    def __exit__(self, *exc):
        self.ws.close()
        self.proc.terminate()

    def call(self, method: str, **params):
        self.seq += 1
        self.ws.send(json.dumps({"id": self.seq, "method": method, "params": params}))
        while True:
            msg = json.loads(self.ws.recv())
            if msg.get("id") == self.seq:
                if "error" in msg:
                    sys.exit(f"{method}: {msg['error']}")
                return msg.get("result", {})

    def eval(self, expr: str):
        res = self.call("Runtime.evaluate", expression=expr, awaitPromise=True, returnByValue=True)
        return res.get("result", {}).get("value")

    def open(self, **params) -> None:
        self.call("Page.navigate", url=url(**params))
        for _ in range(200):
            if self.eval("document.readyState === 'complete'"):
                break
            time.sleep(0.05)
        self.eval("document.fonts.ready.then(() => true)")
        time.sleep(0.15)

    def screenshot(self, out: Path, **params) -> Image.Image:
        self.open(**params)
        data = self.call("Page.captureScreenshot", format="png",
                         clip={"x": 0, "y": 0, "width": 480, "height": 320, "scale": 1})["data"]
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(base64.b64decode(data))
        img = Image.open(out).convert("RGB")
        if img.size != (480, 320):
            sys.exit(f"{out.name}: unexpected size {img.size}")
        return img

    def layout(self, page: str) -> dict:
        self.open(page=page, mode="export")
        text = self.eval("document.getElementById('layout').textContent")
        if not text:
            sys.exit(f"no layout for page {page}")
        return json.loads(text)


def websocket_connect(ws_url: str):
    """Tiny blocking websocket client (text frames only) so the build needs no extra packages."""
    import socket
    import os
    from urllib.parse import urlparse

    u = urlparse(ws_url)
    sock = socket.create_connection((u.hostname, u.port), timeout=60)
    key = base64.b64encode(os.urandom(16)).decode()
    sock.sendall((f"GET {u.path} HTTP/1.1\r\nHost: {u.hostname}:{u.port}\r\nUpgrade: websocket\r\n"
                  f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
    resp = b""
    while b"\r\n\r\n" not in resp:
        resp += sock.recv(1)
    if b" 101 " not in resp.split(b"\r\n", 1)[0]:
        sys.exit("websocket handshake failed")

    class WS:
        def send(self, text: str) -> None:
            payload = text.encode()
            mask = os.urandom(4)
            header = bytearray([0x81])
            n = len(payload)
            if n < 126:
                header.append(0x80 | n)
            elif n < 65536:
                header += bytes([0x80 | 126]) + struct.pack(">H", n)
            else:
                header += bytes([0x80 | 127]) + struct.pack(">Q", n)
            sock.sendall(bytes(header) + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))

        def _exact(self, n: int) -> bytes:
            buf = b""
            while len(buf) < n:
                chunk = sock.recv(n - len(buf))
                if not chunk:
                    raise ConnectionError("websocket closed")
                buf += chunk
            return buf

        def recv(self) -> str:
            data = b""
            while True:
                b0, b1 = self._exact(2)
                n = b1 & 0x7F
                if n == 126:
                    n = struct.unpack(">H", self._exact(2))[0]
                elif n == 127:
                    n = struct.unpack(">Q", self._exact(8))[0]
                data += self._exact(n)
                if b0 & 0x80:
                    return data.decode()

        def close(self) -> None:
            sock.close()

    return WS()


def save_pic(img: Image.Image, idx: int, name: str) -> str:
    PICS.mkdir(parents=True, exist_ok=True)
    fn = f"{idx:03d}_{name}.png"
    img.convert("RGB").save(PICS / fn, optimize=True)
    return fn


def rgb565(hexcol: str) -> int:
    h = hexcol.lstrip("#")
    r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def ident(s: str) -> str:
    return re.sub(r"[^A-Za-z0-9]+", "_", s).upper().strip("_")


# ------------------------------------------------------------------ .zi fonts
def zi_widths(path: Path) -> tuple[str, int, dict[int, int]]:
    data = path.read_bytes()
    height = data[7]
    count = struct.unpack_from("<I", data, 12)[0]
    name_len = struct.unpack_from("<I", data, 32)[0]
    name = data[44:44 + name_len].decode("utf-8", "replace")
    pos = 44 + name_len
    enc_end = data.index(b"utf-8", pos) + 5
    widths: dict[int, int] = {}
    p = enc_end
    for _ in range(count):
        cp, w = struct.unpack_from("<HB", data, p)
        widths[cp] = w
        p += 10
    return name, height, widths


def build_fonts() -> list[tuple[int, str, int, dict[int, int]]]:
    FONTS.mkdir(parents=True, exist_ok=True)
    for f in FONTS.glob("*.zi"):
        f.unlink()
    out = []
    for i, src in enumerate(FONT_FILES):
        name, height, widths = zi_widths(FONT_SRC / src)
        shutil.copy(FONT_SRC / src, FONTS / f"{i}_{src.split('. ', 1)[1]}")
        out.append((i, name, height, widths))
        need = "0123456789:" if i == 6 else "0123456789,°%:-"
        missing = [c for c in need if ord(c) not in widths]
        print(f"font {i}: {name} h={height} glyphs={len(widths)} missing={missing}")
    return out


# ------------------------------------------------------------------ main
def main() -> None:
    with Browser() as br:
        build(br)


def build(br: Browser) -> None:
    PREVIEW.mkdir(parents=True, exist_ok=True)
    if PICS.exists():
        shutil.rmtree(PICS)
    pic_ids: dict[str, int] = {}
    idx = 0
    for name, page, variant in SCREENS:
        img = br.screenshot(PREVIEW / f"{name}.png", page=page, variant=variant, mode="export")
        # The rightmost 30 px are hidden behind the EU frame: keep them plain.
        img.paste((13, 17, 23), (450, 0, 480, 320))
        save_pic(img, idx, name)
        pic_ids[name] = idx
        idx += 1
        print("picture", idx - 1, name)

    # weather sprite (384 x 96)
    img = br.screenshot(PREVIEW / "wx.png", page="wx", mode="export").crop((0, 0, 384, 96))
    pic_ids["wx_sprite"] = idx
    save_pic(img, idx, "wx_sprite")
    idx += 1

    # thermostat ring frames
    x, y, w, h = THERMO_RECT
    ring_first = idx
    t = RING_MIN
    while t <= RING_MAX + 1e-6:
        img = br.screenshot(PREVIEW / "ring.png", page="ring", t=f"{t:.1f}", mode="export").crop((x, y, x + w, y + h))
        save_pic(img, idx, f"ring_{t:.1f}".replace(".", "_"))
        idx += 1
        t += RING_STEP
    ring_count = idx - ring_first
    img = br.screenshot(PREVIEW / "ring.png", page="ring", t="off", mode="export").crop((x, y, x + w, y + h))
    pic_ids["ring_off"] = idx
    save_pic(img, idx, "ring_off")
    idx += 1
    print(f"pictures: {idx} (rings {ring_first}..{ring_first + ring_count - 1})")

    layouts = {p: br.layout(p) for p in LAYOUT_PAGES}
    (CUSTOM / "build" / "layout.json").write_text(json.dumps(layouts, indent=1, ensure_ascii=False), encoding="utf-8")

    fonts = build_fonts()
    write_layout_header(pic_ids, ring_first, ring_count, layouts)
    write_font_header(fonts)
    print("done")


def write_layout_header(pic_ids, ring_first, ring_count, layouts) -> None:
    L = []
    a = L.append
    a("// Generated by custom/tools/build_assets.py from custom/design/ui.html - do not edit.")
    a("#pragma once")
    a("")
    a("#include <cstdint>")
    a("")
    a("namespace esphome::nspanel_ui::gen {")
    a("")
    a("struct Rect {")
    a("  int16_t x, y, w, h;")
    a("  bool contains(int16_t px, int16_t py) const { return px >= x && px < x + w && py >= y && py < y + h; }")
    a("};")
    a("struct TextSpec {")
    a("  Rect r;")
    a("  uint8_t font;    ///< Nextion font id")
    a("  uint16_t color;  ///< RGB565 foreground")
    a("  uint16_t bg;     ///< RGB565 background (xstr sta=1)")
    a("  uint8_t align;   ///< 0 left, 1 centre, 2 right")
    a("};")
    a("struct FillSpec {")
    a("  Rect r;")
    a("  uint16_t color;")
    a("  uint16_t bg;")
    a("};")
    a("")
    a("// Picture ids (Nextion import order)")
    for name, pid in pic_ids.items():
        a(f"constexpr uint8_t PIC_{ident(name)} = {pid};")
    a(f"constexpr uint8_t PIC_RING_FIRST = {ring_first};")
    a(f"constexpr uint8_t PIC_RING_COUNT = {ring_count};")
    a(f"constexpr float RING_MIN = {RING_MIN}f;")
    a(f"constexpr float RING_STEP = {RING_STEP}f;")
    x, y, w, h = THERMO_RECT
    a(f"constexpr Rect RING_RECT = {{{x}, {y}, {w}, {h}}};")
    a("")
    a("// Weather sprite: 48x48 icons, 8 per row")
    a("constexpr int16_t WX_SIZE = 48;")
    a("constexpr int16_t WX_COLS = 8;")
    a("enum WxIcon : uint8_t {")
    for i, n in enumerate(WX_ICONS):
        a(f"  WX_{ident(n)} = {i},")
    a("};")
    a("")
    align = {"l": 0, "c": 1, "r": 2}
    nav_done = False
    for page, lay in layouts.items():
        P = ident(page)
        a(f"// ---- page {page}")
        for tid, t in lay["texts"].items():
            r = t["rect"]
            a(f"constexpr TextSpec T_{P}_{ident(tid)} = {{{{{r[0]}, {r[1]}, {r[2]}, {r[3]}}}, {t['font']}, "
              f"{rgb565(t['color'])}, {rgb565(t['bg'])}, {align[t['align']]}}};")
        for hid, r in lay["hits"].items():
            if hid.startswith("nav_"):
                if nav_done:
                    continue
                a(f"constexpr Rect H_{ident(hid)} = {{{r[0]}, {r[1]}, {r[2]}, {r[3]}}};")
                continue
            a(f"constexpr Rect H_{P}_{ident(hid)} = {{{r[0]}, {r[1]}, {r[2]}, {r[3]}}};")
        if any(h.startswith("nav_") for h in lay["hits"]):
            nav_done = True
        for rid, r in lay["regions"].items():
            a(f"constexpr Rect R_{P}_{ident(rid)} = {{{r[0]}, {r[1]}, {r[2]}, {r[3]}}};")
        for fid, f in lay["fills"].items():
            r = f["rect"]
            a(f"constexpr FillSpec F_{P}_{ident(fid)} = {{{{{r[0]}, {r[1]}, {r[2]}, {r[3]}}}, {rgb565(f['color'])}, {rgb565(f['bg'])}}};")
        a("")
    a("// Colours used at runtime")
    for n, c in {"BG": "#0d1117", "CARD": "#19212e", "TEXT": "#e8edf4", "TEXT2": "#9aa7b8", "TEXT3": "#627084",
                 "AMBER": "#ffb547", "AMBER_TILE": "#33270f", "BLUE": "#5aa9ff", "GREEN": "#4ade80", "RED": "#ff6b6b",
                 "HEAT": "#ff8a3d", "BLACK": "#000000", "SS_TEXT": "#c9d3df", "SS_DIM": "#66748a", "SS_INFO": "#8a97a8",
                 "TRACK_ON": "#1c150a", "CARD_HI": "#232d3d"}.items():
        a(f"constexpr uint16_t C_{n} = {rgb565(c)};")
    a("")
    a("}  // namespace esphome::nspanel_ui::gen")
    (COMPONENT / "generated_layout.h").write_text("\n".join(L) + "\n", encoding="utf-8", newline="\n")


def write_font_header(fonts) -> None:
    L = ["// Generated by custom/tools/build_assets.py from the Sarasa .zi fonts - do not edit.",
         "#pragma once", "", "#include <cstdint>", "", "namespace esphome::nspanel_ui::gen {", "",
         f"constexpr uint16_t FONT_WIDTH_FIRST = 0x{WIDTH_TABLE_FIRST:X};",
         f"constexpr uint16_t FONT_WIDTH_LAST = 0x{WIDTH_TABLE_LAST:X};",
         f"constexpr uint8_t FONT_COUNT = {len(fonts)};", ""]
    heights = []
    for i, name, height, widths in fonts:
        heights.append(str(height))
        vals = [widths.get(cp, 0) for cp in range(WIDTH_TABLE_FIRST, WIDTH_TABLE_LAST + 1)]
        L.append(f"// font {i}: {name}, height {height}")
        L.append(f"constexpr uint8_t FONT{i}_WIDTHS[] = {{")
        for k in range(0, len(vals), 24):
            L.append("    " + ", ".join(str(v) for v in vals[k:k + 24]) + ",")
        L.append("};")
        # fallback advance for characters outside the table (CJK etc.)
        L.append(f"constexpr uint8_t FONT{i}_FALLBACK = {widths.get(0x4E00, height)};")
    L.append("")
    L.append("// Characters above the table that the UI uses (dashes, quotes, bullet, ellipsis, euro)")
    L.append("struct ExtraWidth {")
    L.append("  uint16_t cp;")
    L.append(f"  uint8_t w[{len(fonts)}];")
    L.append("};")
    L.append("constexpr ExtraWidth FONT_EXTRA[] = {")
    for cp in EXTRA_CPS:
        ws = [widths.get(cp, 0) for _, _, _, widths in fonts]
        L.append(f"    {{0x{cp:04X}, {{{', '.join(str(w) for w in ws)}}}}},")
        for (i, _, _, widths) in fonts[:4]:
            if cp not in widths:
                print(f"warning: font {i} has no glyph U+{cp:04X}")
    L.append("};")
    L.append(f"constexpr uint8_t FONT_HEIGHTS[] = {{{', '.join(heights)}}};")
    L.append("constexpr const uint8_t *FONT_WIDTHS[] = {" + ", ".join(f"FONT{i}_WIDTHS" for i, *_ in fonts) + "};")
    L.append("constexpr uint8_t FONT_FALLBACK[] = {" + ", ".join(f"FONT{i}_FALLBACK" for i, *_ in fonts) + "};")
    L += ["", "}  // namespace esphome::nspanel_ui::gen", ""]
    (COMPONENT / "generated_fonts.h").write_text("\n".join(L), encoding="utf-8", newline="\n")


if __name__ == "__main__":
    COMPONENT.mkdir(parents=True, exist_ok=True)
    if "--fonts-only" in sys.argv:
        write_font_header(build_fonts())
    else:
        main()
