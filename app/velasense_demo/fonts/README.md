# Demo font

Noto Sans CJK SC Regular, SIL Open Font License 1.1 (see OFL.txt).
The generated font includes ASCII and every non-ASCII character in demo_ui.c,
at 16px / 4bpp, with Montserrat 14 as icon fallback.
The large original OTF is not required to compile the app.

Upstream font:
https://github.com/notofonts/noto-cjk/blob/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf

Regenerate after changing Chinese labels (requires Node and lv_font_conv 1.5.3):
python3 tools/generate_demo_font.py --font /path/NotoSansCJKsc-Regular.otf --converter /path/node_modules/lv_font_conv/lv_font_conv.js

The generated C source is a font subset and remains under OFL.
