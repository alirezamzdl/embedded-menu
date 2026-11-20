#!/usr/bin/env python3
"""
Font tool for menu_fa (Persian text for embedded-menu).

Fonts live as editable ASCII art in tools/font*.txt and are turned into C
source with the `build` command. To change a glyph, edit the .txt file and
rebuild; no other step is needed.

Commands
--------
  build   FONT.txt NAME          -> C source on stdout (menu_fa_NAME.c)
  render  TTF HEIGHT             -> ASCII-art font on stdout, rendered from a
                                    TrueType font (used once to make font16.txt
                                    from DejaVu Sans)

Typical use:
  python tools/gen_fa_fonts.py build tools/font8.txt  font8  > menu_fa_font8.c
  python tools/gen_fa_fonts.py build tools/font16.txt font16 > menu_fa_font16.c

ASCII-art format
----------------
  ; comment
  font height=8 space=2 gap=1
  glyph 12 ب final
  #######.
  ...                       (exactly `height` lines, all the same width)

  `glyph N` uses the slot numbers below. A slot without a glyph block has no
  glyph (width 0) and is not drawn. Column bit 0 is the top row.

Slot layout (must match menu_fa.h)
----------------------------------
  0   .. 155  letters: LETTER_INDEX * 4 + FORM (isolated, final, medial, initial)
  156 .. 165  Persian digits 0..9
  166 ؟   167 ،   168 ؛   169 «   170 »   171 ٫ (decimal)   172 ٪
  173 لا isolated   174 لا final
  176 .. 270  ASCII 0x20..0x7E (slot 176, the space, is never drawn)
"""

import re
import sys

# ---------------------------------------------------------------------------
# Slot layout
# ---------------------------------------------------------------------------

# (name, presentation forms: isolated, final, initial, medial) in letter order.
# Right-joining letters only have isolated and final forms.
LETTERS = [
    ("ء", (0xFE80,)),
    ("آ", (0xFE81, 0xFE82)),
    ("ا", (0xFE8D, 0xFE8E)),
    ("ب", (0xFE8F, 0xFE90, 0xFE91, 0xFE92)),
    ("پ", (0xFB56, 0xFB57, 0xFB58, 0xFB59)),
    ("ت", (0xFE95, 0xFE96, 0xFE97, 0xFE98)),
    ("ث", (0xFE99, 0xFE9A, 0xFE9B, 0xFE9C)),
    ("ج", (0xFE9D, 0xFE9E, 0xFE9F, 0xFEA0)),
    ("چ", (0xFB7A, 0xFB7B, 0xFB7C, 0xFB7D)),
    ("ح", (0xFEA1, 0xFEA2, 0xFEA3, 0xFEA4)),
    ("خ", (0xFEA5, 0xFEA6, 0xFEA7, 0xFEA8)),
    ("د", (0xFEA9, 0xFEAA)),
    ("ذ", (0xFEAB, 0xFEAC)),
    ("ر", (0xFEAD, 0xFEAE)),
    ("ز", (0xFEAF, 0xFEB0)),
    ("ژ", (0xFB8A, 0xFB8B)),
    ("س", (0xFEB1, 0xFEB2, 0xFEB3, 0xFEB4)),
    ("ش", (0xFEB5, 0xFEB6, 0xFEB7, 0xFEB8)),
    ("ص", (0xFEB9, 0xFEBA, 0xFEBB, 0xFEBC)),
    ("ض", (0xFEBD, 0xFEBE, 0xFEBF, 0xFEC0)),
    ("ط", (0xFEC1, 0xFEC2, 0xFEC3, 0xFEC4)),
    ("ظ", (0xFEC5, 0xFEC6, 0xFEC7, 0xFEC8)),
    ("ع", (0xFEC9, 0xFECA, 0xFECB, 0xFECC)),
    ("غ", (0xFECD, 0xFECE, 0xFECF, 0xFED0)),
    ("ف", (0xFED1, 0xFED2, 0xFED3, 0xFED4)),
    ("ق", (0xFED5, 0xFED6, 0xFED7, 0xFED8)),
    ("ک", (0xFB8E, 0xFB8F, 0xFB90, 0xFB91)),
    ("گ", (0xFB92, 0xFB93, 0xFB94, 0xFB95)),
    ("ل", (0xFEDD, 0xFEDE, 0xFEDF, 0xFEE0)),
    ("م", (0xFEE1, 0xFEE2, 0xFEE3, 0xFEE4)),
    ("ن", (0xFEE5, 0xFEE6, 0xFEE7, 0xFEE8)),
    ("و", (0xFEED, 0xFEEE)),
    ("ه", (0xFEE9, 0xFEEA, 0xFEEB, 0xFEEC)),
    ("ی", (0xFBFC, 0xFBFD, 0xFBFE, 0xFBFF)),
    ("ئ", (0xFE89, 0xFE8A, 0xFE8B, 0xFE8C)),
    ("أ", (0xFE83, 0xFE84)),
    ("إ", (0xFE87, 0xFE88)),
    ("ؤ", (0xFE85, 0xFE86)),
    ("ة", (0xFE93, 0xFE94)),
]

FORMS = ("isolated", "final", "medial", "initial")
SLOT_DIGIT0 = 156
SLOT_QUESTION, SLOT_COMMA, SLOT_SEMICOLON = 166, 167, 168
SLOT_QUOTE_L, SLOT_QUOTE_R, SLOT_DECIMAL, SLOT_PERCENT = 169, 170, 171, 172
SLOT_LAMALEF, SLOT_LAMALEF_FINAL = 173, 174
SLOT_ASCII = 176
SLOT_COUNT = SLOT_ASCII + 95


def letter_slot(index, form):
    return index * 4 + FORMS.index(form)


def presentation_form(index, form):
    """Unicode presentation form for a letter slot (falls back for
    right-joining letters, which have no initial/medial form)."""
    forms = LETTERS[index][1]
    iso = forms[0]
    fin = forms[1] if len(forms) > 1 else iso
    ini = forms[2] if len(forms) > 2 else iso
    med = forms[3] if len(forms) > 3 else fin
    return {"isolated": iso, "final": fin, "initial": ini, "medial": med}[form]


def slot_name(slot):
    if slot < SLOT_DIGIT0:
        return "%s %s" % (LETTERS[slot // 4][0], FORMS[slot % 4])
    if slot < SLOT_DIGIT0 + 10:
        return "digit %s" % chr(0x06F0 + slot - SLOT_DIGIT0)
    names = {SLOT_QUESTION: "؟", SLOT_COMMA: "،", SLOT_SEMICOLON: "؛",
             SLOT_QUOTE_L: "«", SLOT_QUOTE_R: "»", SLOT_DECIMAL: "٫ decimal",
             SLOT_PERCENT: "٪", SLOT_LAMALEF: "لا isolated",
             SLOT_LAMALEF_FINAL: "لا final"}
    if slot in names:
        return names[slot]
    if SLOT_ASCII <= slot < SLOT_COUNT:
        c = chr(0x20 + slot - SLOT_ASCII)
        return "ascii %s" % ("space" if c == " " else c)
    return "unused"


def slot_char(slot):
    """Unicode character to render for a slot (for the `render` command)."""
    if slot < SLOT_DIGIT0:
        return chr(presentation_form(slot // 4, FORMS[slot % 4]))
    if slot < SLOT_DIGIT0 + 10:
        return chr(0x06F0 + slot - SLOT_DIGIT0)
    other = {SLOT_QUESTION: 0x061F, SLOT_COMMA: 0x060C, SLOT_SEMICOLON: 0x061B,
             SLOT_QUOTE_L: 0x00AB, SLOT_QUOTE_R: 0x00BB, SLOT_DECIMAL: 0x066B,
             SLOT_PERCENT: 0x066A, SLOT_LAMALEF: 0xFEFB,
             SLOT_LAMALEF_FINAL: 0xFEFC}
    if slot in other:
        return chr(other[slot])
    if SLOT_ASCII < slot < SLOT_COUNT:
        return chr(0x20 + slot - SLOT_ASCII)
    return None

# ---------------------------------------------------------------------------
# ASCII-art reading / writing
# ---------------------------------------------------------------------------


def read_art(path):
    """Return (header dict, {slot: [row strings]})."""
    header = {}
    glyphs = {}
    cur = None
    with open(path, encoding="utf-8") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.rstrip("\n").rstrip("\r")
            if not line.strip() or line.startswith(";"):
                continue
            if line.startswith("font "):
                for kv in line.split()[1:]:
                    k, v = kv.split("=")
                    header[k] = int(v)
                continue
            if line.startswith("glyph "):
                slot = int(line.split()[1])
                if slot in glyphs:
                    sys.exit("%s:%d: slot %d defined twice" % (path, lineno, slot))
                cur = glyphs.setdefault(slot, [])
                continue
            if cur is None or not re.fullmatch(r"[#.]+", line):
                sys.exit("%s:%d: unexpected line: %r" % (path, lineno, line))
            cur.append(line)
    h = header["height"]
    for slot, rows in glyphs.items():
        if len(rows) != h:
            sys.exit("%s: glyph %d has %d rows, expected %d" % (path, slot, len(rows), h))
        if len({len(r) for r in rows}) != 1:
            sys.exit("%s: glyph %d rows differ in width" % (path, slot))
    return header, glyphs


def write_art(header, glyphs, out, comment=""):
    if comment:
        for c in comment.strip().splitlines():
            out.write(("; %s" % c).rstrip() + "\n")
        out.write("\n")
    out.write("font %s\n\n" % " ".join("%s=%d" % kv for kv in header.items()))
    for slot in sorted(glyphs):
        out.write("glyph %d %s\n" % (slot, slot_name(slot)))
        for row in glyphs[slot]:
            out.write(row + "\n")
        out.write("\n")


def cols_to_rows(cols, height):
    return ["".join("#" if (c >> y) & 1 else "." for c in cols) for y in range(height)]


def rows_to_cols(rows):
    return [sum(1 << y for y, r in enumerate(rows) if r[x] == "#")
            for x in range(len(rows[0]))]


def trim(cols):
    """Remove empty columns on both sides."""
    while cols and cols[0] == 0:
        cols = cols[1:]
    while cols and cols[-1] == 0:
        cols = cols[:-1]
    return cols

# ---------------------------------------------------------------------------
# build: ASCII art -> C
# ---------------------------------------------------------------------------


def cmd_build(path, name):
    header, glyphs = read_art(path)
    h = header["height"]
    if not 1 <= h <= 16:
        sys.exit("height must be 1..16")
    bpc = 1 if h <= 8 else 2

    data = []          # column bytes
    index = {}         # columns tuple -> offset (shares identical glyphs)
    table = []         # (offset, width) per slot
    for slot in range(SLOT_COUNT):
        rows = glyphs.get(slot)
        if not rows:
            table.append((0, 0))
            continue
        cols = tuple(rows_to_cols(rows))
        if cols not in index:
            index[cols] = len(data) // bpc
            for c in cols:
                data.append(c & 0xFF)
                if bpc == 2:
                    data.append(c >> 8)
        table.append((index[cols], len(cols)))

    o = sys.stdout
    o.write("/* Generated by tools/gen_fa_fonts.py from %s. Do not edit:\n"
            " * edit the .txt file and run\n"
            " *   python tools/gen_fa_fonts.py build %s %s > menu_fa_%s.c\n"
            " */\n" % (path.replace("\\", "/"), path.replace("\\", "/"), name, name))
    notice = header_comment(path)
    if notice:
        o.write("\n/*\n%s */\n" % "".join(" * %s\n" % l if l else " *\n" for l in notice))
    o.write('\n#include "menu_fa.h"\n#include "menu_fa_flash.h"\n\n')
    o.write("/* %d bytes of column data, %d pixels high (%d byte%s per column). */\n"
            % (len(data), h, bpc, "" if bpc == 1 else "s"))
    o.write("static const uint8_t columns[%d] MENU_FA_FLASH = {\n" % max(1, len(data)))
    for i in range(0, len(data), 16):
        o.write("    " + ", ".join("0x%02X" % b for b in data[i:i + 16]) + ",\n")
    if not data:
        o.write("    0\n")
    o.write("};\n\n")
    o.write("static const menu_fa_glyph_t glyphs[MENU_FA_SLOT_COUNT] MENU_FA_FLASH = {\n")
    for slot, (off, w) in enumerate(table):
        o.write("    { %5d, %2d },  /* %3d  %s */\n" % (off, w, slot, slot_name(slot)))
    o.write("};\n\n")
    o.write("const menu_fa_font_t menu_fa_%s = {\n" % name)
    o.write("    %d,      /* height */\n" % h)
    o.write("    %d,      /* space width */\n" % header.get("space", 2))
    o.write("    %d,      /* gap width */\n" % header.get("gap", 1))
    o.write("    columns,\n    glyphs\n};\n")


def header_comment(path):
    """Comment lines at the top of the .txt (licence / origin notes)."""
    lines = []
    with open(path, encoding="utf-8") as f:
        for raw in f:
            if raw.startswith(";"):
                lines.append(raw[1:].strip())
            elif raw.strip():
                break
    return lines

# ---------------------------------------------------------------------------
# render: TrueType -> ASCII art
# ---------------------------------------------------------------------------


def cmd_render(ttf, height):
    from PIL import Image, ImageDraw, ImageFont

    chars = {s: slot_char(s) for s in range(SLOT_COUNT) if slot_char(s)}

    def render(size):
        font = ImageFont.truetype(ttf, size)
        out = {}
        for slot, ch in chars.items():
            img = Image.new("1", (size * 4, size * 3), 0)
            d = ImageDraw.Draw(img)
            d.fontmode = "1"                       # hinted, not anti-aliased
            d.text((size, size * 2), ch, font=font, fill=1, anchor="ls")
            out[slot] = img
        return out

    # Largest size whose ink (all glyphs) fits in `height` rows.
    best = None
    for size in range(height // 2, height * 2):
        imgs = render(size)
        top, bottom = 10 ** 9, -1
        for img in imgs.values():
            bb = img.getbbox()
            if bb:
                top, bottom = min(top, bb[1]), max(bottom, bb[3])
        if bottom - top <= height:
            best = (size, imgs, top)
        else:
            break
    if best is None:
        sys.exit("no size fits")
    size, imgs, top = best

    glyphs = {}
    for slot, img in imgs.items():
        w, _ = img.size
        rows = []
        for y in range(top, top + height):
            rows.append("".join("#" if img.getpixel((x, y)) else "." for x in range(w)))
        cols = trim(rows_to_cols(rows))
        if cols:
            glyphs[slot] = cols_to_rows(cols, height)

    write_art({"height": height, "space": max(2, height // 4), "gap": 1}, glyphs,
              sys.stdout,
              "%d-pixel Persian + Latin font for menu_fa, rendered from\n"
              "DejaVu Sans at %d px with tools/gen_fa_fonts.py.\n"
              "\n"
              "DejaVu fonts: (c) Bitstream (Bitstream Vera licence), DejaVu changes\n"
              "are in the public domain, Arabic glyphs (c) Tavmjong Bah (Arev).\n"
              "This bitmap font is a derived work and is not named Bitstream,\n"
              "Vera or DejaVu. Full licence: https://dejavu-fonts.github.io/License.html"
              % (height, size))

# ---------------------------------------------------------------------------


def main(argv):
    if len(argv) >= 3 and argv[0] == "build":
        cmd_build(argv[1], argv[2])
    elif len(argv) >= 3 and argv[0] == "render":
        cmd_render(argv[1], int(argv[2]))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    main(sys.argv[1:])
