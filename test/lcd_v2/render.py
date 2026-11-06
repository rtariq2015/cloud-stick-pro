"""Actual LCD adapter tests and local raster preview; no hardware/network."""
from pathlib import Path
import binascii
import hashlib
import html
import json
import os
import re
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "artifacts/lcd-v2"
PREVIEW = OUT / "preview"
TFT = Path(os.environ.get("CSP_LCD_TFT_SOURCE",
    "/home/home/tool.plugin-builder/build/research/cloud-stick-product-release-stage/.pio/libdeps/tdongle_s3_product_rc2/TFT_eSPI"))
PINS = {
    "library.json": "15308ca95e4d98ec7f841858fcea02fd252e208f549c4a349d1e236f11582ffa",
    "Fonts/Font16.h": "bb6c40ee31c365f16ac1265c563f075c409a8d6c021789b58fd16ef4b6e7bb14",
    "Fonts/Font16.c": "27f47a9506aa1cc25bb3acf8b50a6458d567babcbcc53ff6d34a6aae6ed85e0c",
    "Fonts/glcdfont.c": "bde369bf045a11a092f06d6073d44039c72515d8bc83353a87b8b8032e450d7c",
    "TFT_eSPI.cpp": "a0b1e1a46835df5cb72be21f75fa587f4a68f263b1cb466ec7760b44248a50ae",
}
NAMES = ["up-to-date", "one-hour", "setup-hint", "local-status", "checking-usb",
         "uploading", "verifying", "saving", "retry", "cloud-full", "cloud-login",
         "wifi-offline", "no-card", "card-fault", "waiting-idle", "scheduled",
         "finding-files", "preparing", "partial-scan", "inactive-last-transfer", "paused",
         "setup-instructions", "setup-review", "setup-blocked"]


def sha(file):
    return hashlib.sha256(file.read_bytes()).hexdigest()


def png(file, width, height, rgb):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", binascii.crc32(kind + data) & 0xffffffff)
    rows = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    file.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def previews():
    cells = []
    width, height, stride = 4 * 336, ((len(NAMES) + 3) // 4) * 176, 4 * 336 * 3
    sheet = bytearray([12, 18, 27] * (width * height))
    fonts = (TFT / "Fonts/glcdfont.c").read_text().split("{", 1)[1].split("};", 1)[0]
    glyphs = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", fonts))
    assert len(glyphs) >= 128 * 5
    for index, name in enumerate(NAMES):
        header, data = (PREVIEW / (name + ".ppm")).read_bytes().split(b"255\n", 1)
        assert header == b"P6\n160 80\n" and len(data) == 160 * 80 * 3
        png(PREVIEW / (name + ".png"), 160, 80, data)
        left, top = (index % 4) * 336 + 8, (index // 4) * 176 + 8
        for y in range(160):
            for x in range(320):
                source = ((y // 2) * 160 + x // 2) * 3
                target = (top + y) * stride + (left + x) * 3
                sheet[target:target + 3] = data[source:source + 3]
        # Caption is outside the simulated panel and uses the same pinned Font1.
        for at, char in enumerate(name):
            for x in range(5):
                for y in range(8):
                    if glyphs[ord(char) * 5 + x] & (1 << y):
                        target = (top + 160 + y) * stride + (left + at * 6 + x) * 3
                        sheet[target:target + 3] = b"\xc9\xd4\xe3"
        cells.append('<figure><img width="480" height="240" src="' + name + '.png" alt="' + name
                     + '"><figcaption>' + html.escape(name) + "</figcaption></figure>")
    png(PREVIEW / "contact-sheet.png", width, height, sheet)
    (PREVIEW / "index.html").write_text("""<!doctype html><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>LCD source preview</title>
<style>body{background:#0c121b;color:#c9d4e3;font:16px system-ui;margin:24px}main{display:flex;flex-wrap:wrap;gap:18px}figure{margin:0}img{image-rendering:pixelated;max-width:100%;height:auto}figcaption{margin:8px 0 18px}p{max-width:900px}</style>
<h1>Cloud Stick Pro — LCD source preview</h1>
<p>Actual lcd.cpp adapter with pinned TFT_eSPI2.5.43 Font1/Font2 glyphs on a native mock160×80 framebuffer. Images are enlarged3×. Navy/blue/white phase cards keep amber faults distinct; IDLE and WAIT describe the current service state while verified counts are retained backup records. Rounded corners/colors are approximations; brightness, SPI behavior and physical readability remain untested. All data is synthetic. No device, network or Site was used.</p><main>""" + "".join(cells) + "</main>\n")


def main():
    PREVIEW.mkdir(parents=True, exist_ok=True)
    assert json.loads((TFT / "library.json").read_text())["version"] == "2.5.43"
    assert all(sha(TFT / name) == value for name, value in PINS.items()), "Pinned TFT font/source bytes changed"
    files = [ROOT / "src/lcd.cpp", ROOT / "include/pro_lcd_v2.h", ROOT / "include/pro_display.h",
             ROOT / "include/lcd.h", ROOT / "include/pro_setup_text.h"]
    files += [p for p in (ROOT / "test/lcd_v2").rglob("*") if p.is_file()]
    before = {str(p.relative_to(ROOT)): sha(p) for p in sorted(files)}
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:abort_on_error=1",
               UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    records = []
    def run(name, command):
        args = list(map(str, command))
        result = subprocess.run(args, cwd=ROOT, env=env, text=True, capture_output=True, timeout=120)
        output = result.stdout + result.stderr
        (OUT / (name + ".log")).write_text(output)
        diagnostic = bool(re.search(r"AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:", output))
        passed = result.returncode == 0 and not result.stderr and not diagnostic
        records.append(dict(name=name, passed=passed, exit=result.returncode,
                            command=args, stderr_empty=not result.stderr, sanitizer_diagnostic=diagnostic))
        print(name, "PASS" if passed else "FAIL", flush=True)
        if not passed:
            raise RuntimeError(output[-10000:])
    flags = ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-format",
             "-Wno-unused-function", "-g", "-O1", "-fno-omit-frame-pointer",
             "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
             "-Itest/lcd_v2/stubs", "-Iinclude", "-I" + str(TFT),
             "test/lcd_v2/renderer.test.cpp"]
    run("renderer-enabled-build", [*flags, "-DCSP_ENABLE_LCD_V2=1", "-o", OUT / "renderer"])
    run("renderer-enabled", [OUT / "renderer", PREVIEW])
    run("renderer-default-build", [*flags, "-o", OUT / "renderer-default"])
    run("renderer-default", [OUT / "renderer-default", PREVIEW])
    assert before == {str(p.relative_to(ROOT)): sha(p) for p in sorted(files)}, "Source changed during render gate"
    assert all(sha(TFT / name) == value for name, value in PINS.items()), "Dependency changed during render gate"
    previews()
    report = dict(schema="cloud-stick-lcd-renderer-source-tests/v1", passed=True,
                  physical=False, device=False, network=False, site=False, source_stable=True,
                  actual_source="src/lcd.cpp", source_sha256=before,
                  dependency=dict(name="TFT_eSPI", version="2.5.43", readonly_directory=str(TFT), sha256=PINS),
                  records=records, sanitizers=["ASan", "UBSan", "LeakSanitizer"],
                  allocation_check="Zero C++ new/new[] during actual normal/pro and setup-text renderer calls; mock TFT uses fixed buffers",
                  native_frame_bytes=88, preview="preview/index.html", contact_sheet="preview/contact-sheet.png",
                  panels=NAMES, limitations=["No target/PIO build", "TFT/SPI/Arduino transport mocked",
                    "Rounded-corner raster approximation", "Native legacy formatter uses -Wno-format for ESP32/native width differences",
                    "Actual font bitmap/advance metrics used; physical offsets/brightness/visibility/timing not measured",
                    "No claim about target TFT internal allocations or stack/heap"])
    (OUT / "renderer-results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS actual renderer geometry and local{len(NAMES)}-panel preview", flush=True)


if __name__ == "__main__":
    main()
