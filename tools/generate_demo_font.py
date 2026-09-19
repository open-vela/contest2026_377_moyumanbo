#!/usr/bin/env python3
"""Regenerate the display demo's licensed Noto Sans CJK subset."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--font", required=True, help="NotoSansCJKsc-Regular.otf")
parser.add_argument("--converter", required=True, help="lv_font_conv.js (1.5.3)")
parser.add_argument("--node", default="node")
args = parser.parse_args()
app = Path(__file__).resolve().parents[1] / "app/velasense_demo"
symbols = "".join(sorted({c for c in (app / "demo_ui.c").read_text(encoding="utf-8")
                         if ord(c) > 127}))
subprocess.run([
    args.node, args.converter, "--font", args.font,
    "--size", "16", "--bpp", "4", "--range", "0x20-0x7e",
    "--symbols", symbols, "--format", "lvgl", "--no-compress", "--no-prefilter",
    "--lv-include", "lvgl/lvgl.h", "--lv-font-name", "lv_font_velasense_16",
    "--lv-fallback", "lv_font_montserrat_14",
    "--output", str(app / "fonts/lv_font_velasense_16.c"),
], check=True)
