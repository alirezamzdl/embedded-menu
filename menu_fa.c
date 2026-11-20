/**
 * @file menu_fa.c
 * @brief Persian (Farsi) text shaping and rendering. See menu_fa.h.
 *
 * How it works
 * ============
 *
 * 1. Letter forms
 * ---------------
 * Persian letters change shape depending on whether they connect to the
 * letter before and/or after them, so each letter has up to four forms:
 *
 *   isolated  - connects to neither neighbour        e.g. ب alone
 *   initial   - connects only to the next letter      e.g. the ب in "با"
 *   medial    - connects to both neighbours           e.g. the ب in "نبض"
 *   final     - connects only to the previous letter  e.g. the ب in "سب"
 *
 * Whether two neighbours connect depends on their "joining type":
 *
 *   DUAL   - joins on both sides (most letters: ب ت س ...)
 *   RIGHT  - joins only to the letter before it, never to the next one
 *            (ا آ د ذ ر ز ژ و ة)
 *   NONE   - never joins (ء)
 *
 * A letter connects to the letter before it if that letter is DUAL and the
 * letter itself is not NONE. It connects to the letter after it if it is
 * DUAL and the next letter is not NONE. Anything that is not a Persian
 * letter (space, digit, Latin letter, punctuation, half-space U+200C)
 * breaks the chain.
 *
 * ل followed by ا (or آ أ إ) is drawn as the single ligature لا, in its
 * isolated or final form depending on whether ل joins the letter before.
 *
 * 2. Direction (a small subset of the Unicode bidi algorithm)
 * -----------------------------------------------------------
 * The line is right-to-left. Text is stored in logical order (the order it
 * is typed), which for Persian is also the drawing order when we draw from
 * the right edge towards the left. Two kinds of runs read left to right and
 * are therefore drawn back to front:
 *
 *   - Numbers: digits with single separators between them (12:30, 3.5,
 *     1404/05/01) and an optional trailing % or ٪. Drawn with Persian
 *     digits.
 *   - Latin runs: from a Latin letter up to the last Latin letter or digit
 *     before the next Persian letter. Spaces and punctuation inside the run
 *     belong to it ("USB 2.0", "Wi-Fi"); trailing ones don't, except a
 *     closing bracket that matches one opened inside the run
 *     ("WiFi (2.4GHz)"). A number directly followed by a Latin letter
 *     ("2.4GHz") joins the Latin run. Drawn with the font's Latin glyphs
 *     and ASCII digits.
 *
 * Brackets and quotes outside Latin runs are mirrored: in right-to-left
 * text "(" is the opening bracket on the right, so it is drawn as ")".
 *
 * 3. Tokens
 * ---------
 * Walking the text produces tokens in drawing order: a glyph slot, a gap
 * (blank columns put to the right of every glyph that does not connect to
 * the previous one, so word pieces and Latin letters don't touch) or a
 * space. menu_fa_width(), menu_fa_draw() and menu_fa_shape() all use the
 * same walk, so they always agree.
 *
 * 4. Fonts
 * --------
 * Every font uses the same slot numbers (see menu_fa.h), so shaping is
 * independent of the font. Fonts are generated from ASCII-art files by
 * tools/gen_fa_fonts.py: menu_fa_font8.c and menu_fa_font16.c.
 */
#include "menu_fa.h"
#include "menu_fa_flash.h"
#include <stddef.h>

/* ------------------------------------------------------------------------ */
/* Letters                                                                  */
/* ------------------------------------------------------------------------ */

/** Joining types, see "Letter forms" above. */
enum { FA_JOIN_NONE = 0, FA_JOIN_RIGHT, FA_JOIN_DUAL };

/** Letter indices that the code refers to by name (positions in fa_letters). */
enum {
    FA_L_ALEF_MADDA = 1,   /* آ */
    FA_L_ALEF       = 2,   /* ا */
    FA_L_LAM        = 28,  /* ل */
    FA_L_ALEF_HAMZA_ABOVE = 35,  /* أ */
    FA_L_ALEF_HAMZA_BELOW = 36   /* إ */
};

typedef struct {
    uint16_t cp;      /**< Unicode code point */
    uint8_t  letter;  /**< Letter index: glyph slots are letter * 4 + form */
    uint8_t  join;    /**< FA_JOIN_* */
} fa_letter_t;

/*
 * Every letter the fonts can draw. The first 39 entries are in letter-index
 * order (they define the slot layout, which must match tools/gen_fa_fonts.py).
 * The last entries are Arabic code points drawn with a Persian letter's
 * glyphs (ك -> ک, ي ى -> ی, ھ ہ -> ه), so text copied from Arabic sources
 * still renders.
 */
static const fa_letter_t fa_letters[] MENU_FA_FLASH = {
    /*  cp     letter  joining                       */
    { 0x0621,  0, FA_JOIN_NONE  },  /* ء hamza       */
    { 0x0622,  1, FA_JOIN_RIGHT },  /* آ alef madda  */
    { 0x0627,  2, FA_JOIN_RIGHT },  /* ا alef        */
    { 0x0628,  3, FA_JOIN_DUAL  },  /* ب be          */
    { 0x067E,  4, FA_JOIN_DUAL  },  /* پ pe          */
    { 0x062A,  5, FA_JOIN_DUAL  },  /* ت te          */
    { 0x062B,  6, FA_JOIN_DUAL  },  /* ث se          */
    { 0x062C,  7, FA_JOIN_DUAL  },  /* ج jim         */
    { 0x0686,  8, FA_JOIN_DUAL  },  /* چ che         */
    { 0x062D,  9, FA_JOIN_DUAL  },  /* ح he          */
    { 0x062E, 10, FA_JOIN_DUAL  },  /* خ khe         */
    { 0x062F, 11, FA_JOIN_RIGHT },  /* د dal         */
    { 0x0630, 12, FA_JOIN_RIGHT },  /* ذ zal         */
    { 0x0631, 13, FA_JOIN_RIGHT },  /* ر re          */
    { 0x0632, 14, FA_JOIN_RIGHT },  /* ز ze          */
    { 0x0698, 15, FA_JOIN_RIGHT },  /* ژ zhe         */
    { 0x0633, 16, FA_JOIN_DUAL  },  /* س sin         */
    { 0x0634, 17, FA_JOIN_DUAL  },  /* ش shin        */
    { 0x0635, 18, FA_JOIN_DUAL  },  /* ص sad         */
    { 0x0636, 19, FA_JOIN_DUAL  },  /* ض zad         */
    { 0x0637, 20, FA_JOIN_DUAL  },  /* ط ta          */
    { 0x0638, 21, FA_JOIN_DUAL  },  /* ظ za          */
    { 0x0639, 22, FA_JOIN_DUAL  },  /* ع eyn         */
    { 0x063A, 23, FA_JOIN_DUAL  },  /* غ gheyn       */
    { 0x0641, 24, FA_JOIN_DUAL  },  /* ف fe          */
    { 0x0642, 25, FA_JOIN_DUAL  },  /* ق ghaf        */
    { 0x06A9, 26, FA_JOIN_DUAL  },  /* ک kaf         */
    { 0x06AF, 27, FA_JOIN_DUAL  },  /* گ gaf         */
    { 0x0644, 28, FA_JOIN_DUAL  },  /* ل lam         */
    { 0x0645, 29, FA_JOIN_DUAL  },  /* م mim         */
    { 0x0646, 30, FA_JOIN_DUAL  },  /* ن nun         */
    { 0x0648, 31, FA_JOIN_RIGHT },  /* و vav         */
    { 0x0647, 32, FA_JOIN_DUAL  },  /* ه he          */
    { 0x06CC, 33, FA_JOIN_DUAL  },  /* ی ye          */
    { 0x0626, 34, FA_JOIN_DUAL  },  /* ئ ye hamza    */
    { 0x0623, 35, FA_JOIN_RIGHT },  /* أ alef hamza above */
    { 0x0625, 36, FA_JOIN_RIGHT },  /* إ alef hamza below */
    { 0x0624, 37, FA_JOIN_RIGHT },  /* ؤ vav hamza   */
    { 0x0629, 38, FA_JOIN_RIGHT },  /* ة te marbuta  */
    /* Arabic code points drawn with Persian glyphs */
    { 0x0643, 26, FA_JOIN_DUAL  },  /* ك -> ک       */
    { 0x064A, 33, FA_JOIN_DUAL  },  /* ي -> ی       */
    { 0x0649, 33, FA_JOIN_DUAL  },  /* ى -> ی       */
    { 0x06BE, 32, FA_JOIN_DUAL  },  /* ھ -> ه       */
    { 0x06C1, 32, FA_JOIN_DUAL  },  /* ہ -> ه       */
};

#define FA_LETTER_COUNT (sizeof(fa_letters) / sizeof(fa_letters[0]))

/* Special code points. */
#define FA_CP_ZWNJ     0x200Cu  /* zero-width non-joiner (half-space): breaks joining, no width */
#define FA_CP_ZWJ      0x200Du  /* zero-width joiner: ignored */
#define FA_CP_NBSP     0x00A0u  /* no-break space: drawn as a space */
#define FA_CP_INVALID  0xFFFDu  /* what utf8_next() returns for malformed UTF-8 */
#define FA_NO_SLOT     0xFFFFu  /* "no glyph" result of the slot lookups */

/* ------------------------------------------------------------------------ */
/* UTF-8                                                                    */
/* ------------------------------------------------------------------------ */

/**
 * Decode one UTF-8 character at *p and advance *p past it.
 * Returns 0 at the end of the string, without advancing. Malformed bytes
 * return FA_CP_INVALID and are skipped, so a bad string can never make the
 * walk run past the terminating '\0'.
 */
static uint32_t utf8_next(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t cp;
    uint8_t extra, i;

    if (s[0] == 0) return 0;

    if (s[0] < 0x80u)                 { cp = s[0];         extra = 0; }
    else if ((s[0] & 0xE0u) == 0xC0u) { cp = s[0] & 0x1Fu; extra = 1; }
    else if ((s[0] & 0xF0u) == 0xE0u) { cp = s[0] & 0x0Fu; extra = 2; }
    else if ((s[0] & 0xF8u) == 0xF0u) { cp = s[0] & 0x07u; extra = 3; }
    else {
        *p += 1;                      /* stray continuation or invalid lead byte */
        return FA_CP_INVALID;
    }

    for (i = 1; i <= extra; i++) {
        if ((s[i] & 0xC0u) != 0x80u) {
            /* Truncated sequence. This also stops at '\0', which is not a
             * continuation byte. */
            *p += i;
            return FA_CP_INVALID;
        }
        cp = (cp << 6) | (s[i] & 0x3Fu);
    }
    *p += 1u + extra;
    return cp;
}

/** Decode the character at p without advancing. */
static uint32_t utf8_peek(const char *p)
{
    return utf8_next(&p);
}

/** Step back from `p` to the start of the previous UTF-8 character, not before `start`. */
static const char *utf8_prev(const char *p, const char *start)
{
    do {
        p--;
    } while (p > start && (((unsigned char)*p) & 0xC0u) == 0x80u);
    return p;
}

/* ------------------------------------------------------------------------ */
/* Character classes                                                        */
/* ------------------------------------------------------------------------ */

/** 0..9 if cp is an ASCII, Persian or Arabic-Indic digit, otherwise -1. */
static int digit_value(uint32_t cp)
{
    if (cp >= '0' && cp <= '9')         return (int)(cp - '0');
    if (cp >= 0x06F0u && cp <= 0x06F9u) return (int)(cp - 0x06F0u);  /* Persian ۰..۹ */
    if (cp >= 0x0660u && cp <= 0x0669u) return (int)(cp - 0x0660u);  /* Arabic  ٠..٩ */
    return -1;
}

static bool is_latin_letter(uint32_t cp)
{
    return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
}

static bool is_space(uint32_t cp)
{
    return cp == ' ' || cp == FA_CP_NBSP;
}

/** Index of cp in fa_letters, or -1 if it is not a Persian letter. */
static int find_letter(uint32_t cp)
{
    unsigned i;
    if (cp < 0x0600u || cp > 0x06FFu) return -1;   /* quick reject */
    for (i = 0; i < FA_LETTER_COUNT; i++) {
        if (MENU_FA_READ_WORD(&fa_letters[i].cp) == cp) return (int)i;
    }
    return -1;
}

/** Joining type of cp. Anything that is not a Persian letter is FA_JOIN_NONE. */
static uint8_t join_type(uint32_t cp)
{
    int i = find_letter(cp);
    return (i < 0) ? (uint8_t)FA_JOIN_NONE : MENU_FA_READ_BYTE(&fa_letters[i].join);
}

/** Letter index of cp (see fa_letters), or -1. */
static int letter_index(uint32_t cp)
{
    int i = find_letter(cp);
    return (i < 0) ? -1 : (int)MENU_FA_READ_BYTE(&fa_letters[i].letter);
}

/** True if cp may appear inside a number, between two digits. */
static bool number_separator(uint32_t cp)
{
    return cp == ':' || cp == '.' || cp == '/' || cp == '-' || cp == ',' ||
           cp == 0x066Bu;   /* ٫ Arabic decimal separator */
}

/** The mirrored counterpart of a bracket or quote, or cp itself. */
static uint32_t mirror(uint32_t cp)
{
    switch (cp) {
    case '(':     return ')';
    case ')':     return '(';
    case '[':     return ']';
    case ']':     return '[';
    case '{':     return '}';
    case '}':     return '{';
    case '<':     return '>';
    case '>':     return '<';
    case 0x00ABu: return 0x00BBu;   /* « -> » */
    case 0x00BBu: return 0x00ABu;   /* » -> « */
    default:      return cp;
    }
}

/**
 * Glyph slot for a character that is not a Persian letter: digits (as
 * Persian digits if `persian_digits`, else ASCII), ASCII punctuation and
 * Latin letters, and Persian punctuation. FA_NO_SLOT if there is none.
 */
static uint16_t other_slot(uint32_t cp, bool persian_digits)
{
    int d = digit_value(cp);
    if (d >= 0) {
        if (persian_digits || cp > 0x7Fu) return (uint16_t)(MENU_FA_SLOT_DIGIT0 + (unsigned)d);
        return (uint16_t)(MENU_FA_SLOT_ASCII + (cp - 0x20u));
    }
    if (cp > 0x20u && cp < 0x7Fu) return (uint16_t)(MENU_FA_SLOT_ASCII + (cp - 0x20u));
    switch (cp) {
    case 0x061Fu: return MENU_FA_SLOT_QUESTION;   /* ؟ */
    case 0x060Cu: return MENU_FA_SLOT_COMMA;      /* ، */
    case 0x061Bu: return MENU_FA_SLOT_SEMICOLON;  /* ؛ */
    case 0x00ABu: return MENU_FA_SLOT_QUOTE_L;    /* « */
    case 0x00BBu: return MENU_FA_SLOT_QUOTE_R;    /* » */
    case 0x066Bu: return MENU_FA_SLOT_DECIMAL;    /* ٫ */
    case 0x066Au: return MENU_FA_SLOT_PERCENT;    /* ٪ */
    default:      return FA_NO_SLOT;
    }
}

/* ------------------------------------------------------------------------ */
/* Run detection                                                            */
/* ------------------------------------------------------------------------ */

/**
 * `p` points just after a digit. Return the end of the number that digit
 * starts: more digits, single separators between digits, and an optional
 * trailing % or ٪.
 */
static const char *number_end(const char *p)
{
    for (;;) {
        const char *tmp = p;
        uint32_t c = utf8_next(&tmp);
        if (digit_value(c) >= 0) {
            p = tmp;
        } else if (number_separator(c) && digit_value(utf8_peek(tmp)) >= 0) {
            p = tmp;                  /* "12:" only counts if a digit follows */
        } else {
            if (c == '%' || c == 0x066Au) p = tmp;
            return p;
        }
    }
}

/**
 * `here` points at the first character of a Latin run. Return the end of
 * the run: just after the last Latin letter or digit before the next
 * Persian letter (or the end of the string).
 */
static const char *latin_run_end(const char *here)
{
    const char *p = here;
    const char *end = here;
    int open = 0;          /* brackets opened inside the run and not closed yet */
    uint32_t c;

    for (;;) {
        c = utf8_next(&p);
        if (c == 0 || find_letter(c) >= 0) break;
        if (is_latin_letter(c) || digit_value(c) >= 0) end = p;
        if (c == '(' || c == '[' || c == '{') open++;
        if ((c == ')' || c == ']' || c == '}') && open > 0) open--;
    }

    /* Paired brackets: a closing bracket right after the run that matches
     * one opened inside it belongs to the run, so "WiFi (2.4GHz)" keeps its
     * ")" on the Latin side. Recount up to `end` first. */
    open = 0;
    for (p = here; p < end; ) {
        c = utf8_next(&p);
        if (c == '(' || c == '[' || c == '{') open++;
        if ((c == ')' || c == ']' || c == '}') && open > 0) open--;
    }
    for (p = end; open > 0; ) {
        const char *before = p;
        c = utf8_next(&p);
        if (c == ')' || c == ']' || c == '}') {
            open--;
            end = p;
        } else if (!(c == '.' || c == ',' || c == '!' || c == '?')) {
            p = before;
            break;          /* only closers and light punctuation may follow */
        }
    }
    return end;
}

/* ------------------------------------------------------------------------ */
/* The walk: text -> tokens in drawing order                                */
/* ------------------------------------------------------------------------ */

/** Receives the tokens of fa_walk() one by one. */
typedef void (*fa_emit_fn)(void *ctx, uint16_t token);

/** Shared state of one walk. */
typedef struct {
    fa_emit_fn emit;
    void      *ctx;
    uint16_t   count;
} fa_walk_t;

static void put(fa_walk_t *w, uint16_t token)
{
    w->emit(w->ctx, token);
    w->count++;
}

/** Emit a glyph that does not join its neighbours: gap, then the glyph. */
static void put_alone(fa_walk_t *w, uint16_t slot)
{
    put(w, MENU_FA_TOK_GAP);
    put(w, slot);
}

/**
 * Emit the left-to-right run [start, end) back to front.
 * `persian_digits` selects Persian or ASCII digit glyphs.
 */
static void put_ltr_run(fa_walk_t *w, const char *start, const char *end,
                        bool persian_digits)
{
    const char *q = end;
    while (q > start) {
        const char *tmp;
        uint32_t c;
        uint16_t slot;

        q = utf8_prev(q, start);
        tmp = q;
        c = utf8_next(&tmp);

        if (is_space(c)) {
            put(w, MENU_FA_TOK_SPACE);
        } else if (c == FA_CP_ZWNJ || c == FA_CP_ZWJ) {
            /* nothing */
        } else {
            slot = other_slot(c, persian_digits);
            if (slot != FA_NO_SLOT) put_alone(w, slot);
        }
    }
}

/**
 * Walk `text` and call emit() once per token, in drawing order (right to
 * left). Returns the number of tokens.
 */
static uint16_t fa_walk(const char *text, fa_emit_fn emit, void *ctx)
{
    fa_walk_t w;
    const char *p = text;
    bool prev_dual = false;   /* the previous character is a letter that joins forward */

    w.emit = emit;
    w.ctx = ctx;
    w.count = 0;
    if (text == NULL) return 0;

    for (;;) {
        const char *here = p;          /* start of the current character */
        uint32_t cp = utf8_next(&p);
        int li;

        if (cp == 0) break;

        /* ---- Numbers and Latin runs: drawn back to front ---- */
        if (digit_value(cp) >= 0) {
            const char *end = number_end(p);
            if (is_latin_letter(utf8_peek(end))) {
                /* "2.4GHz": the number belongs to the Latin run after it. */
                end = latin_run_end(end);
                put_ltr_run(&w, here, end, false);
            } else {
                put_ltr_run(&w, here, end, true);
            }
            p = end;
            prev_dual = false;
            continue;
        }
        if (is_latin_letter(cp)) {
            const char *end = latin_run_end(here);
            put_ltr_run(&w, here, end, false);
            p = end;
            prev_dual = false;
            continue;
        }

        /* ---- Spaces ---- */
        if (is_space(cp)) {
            put(&w, MENU_FA_TOK_SPACE);
            prev_dual = false;
            continue;
        }

        /* ---- Half-space: no width, but letters on each side don't join ---- */
        if (cp == FA_CP_ZWNJ) {
            prev_dual = false;
            continue;
        }
        if (cp == FA_CP_ZWJ) continue;

        /* ---- Persian letters ---- */
        li = letter_index(cp);
        if (li >= 0) {
            uint8_t join = join_type(cp);
            bool joins_prev = prev_dual && join != FA_JOIN_NONE;
            bool joins_next = false;
            uint32_t next = utf8_peek(p);
            int next_li = letter_index(next);
            uint8_t form;

            /* لا ligature: ل followed by any alef. */
            if (li == FA_L_LAM &&
                (next_li == FA_L_ALEF || next_li == FA_L_ALEF_MADDA ||
                 next_li == FA_L_ALEF_HAMZA_ABOVE || next_li == FA_L_ALEF_HAMZA_BELOW)) {
                if (!joins_prev) put(&w, MENU_FA_TOK_GAP);
                put(&w, joins_prev ? MENU_FA_SLOT_LAMALEF_FINAL : MENU_FA_SLOT_LAMALEF);
                (void)utf8_next(&p);    /* the alef is part of the ligature */
                prev_dual = false;      /* alef never joins forward */
                continue;
            }

            if (join == FA_JOIN_DUAL) joins_next = join_type(next) != FA_JOIN_NONE;

            if (joins_prev) form = joins_next ? MENU_FA_FORM_MEDIAL  : MENU_FA_FORM_FINAL;
            else            form = joins_next ? MENU_FA_FORM_INITIAL : MENU_FA_FORM_ISOLATED;

            /* A new word piece starts here: leave a gap to its right. */
            if (!joins_prev) put(&w, MENU_FA_TOK_GAP);
            put(&w, MENU_FA_SLOT_LETTER((unsigned)li, form));

            prev_dual = (join == FA_JOIN_DUAL);
            continue;
        }

        /* ---- Punctuation in right-to-left text: mirrored ---- */
        {
            uint16_t slot = other_slot(mirror(cp), true);
            if (slot != FA_NO_SLOT) put_alone(&w, slot);
            /* Characters no font has (emoji...) are skipped. */
            prev_dual = false;
        }
    }
    return w.count;
}

/* ------------------------------------------------------------------------ */
/* Glyph access                                                             */
/* ------------------------------------------------------------------------ */

uint8_t menu_fa_glyph_width(const menu_fa_font_t *font, uint16_t slot)
{
    if (font == NULL || slot >= MENU_FA_SLOT_COUNT) return 0;
    return MENU_FA_READ_BYTE(&font->glyphs[slot].width);
}

uint16_t menu_fa_glyph_column(const menu_fa_font_t *font, uint16_t slot, uint8_t col)
{
    uint8_t width = menu_fa_glyph_width(font, slot);
    uint16_t index;

    if (col >= width) return 0;
    /* Stored left to right; col counts from the right edge. */
    index = (uint16_t)(MENU_FA_READ_WORD(&font->glyphs[slot].offset) + (width - 1u - col));
    if (font->height <= 8u) return MENU_FA_READ_BYTE(&font->columns[index]);
    return (uint16_t)(MENU_FA_READ_BYTE(&font->columns[2u * index]) |
                      (MENU_FA_READ_BYTE(&font->columns[2u * index + 1u]) << 8));
}

/** Number of columns a token takes in `font`. */
static uint8_t token_width(const menu_fa_font_t *font, uint16_t token)
{
    if (token == MENU_FA_TOK_GAP)   return font->gap_width;
    if (token == MENU_FA_TOK_SPACE) return font->space_width;
    return menu_fa_glyph_width(font, token);
}

/* ------------------------------------------------------------------------ */
/* Measuring and shaping                                                    */
/* ------------------------------------------------------------------------ */

typedef struct {
    const menu_fa_font_t *font;
    int                   width;
} fa_width_ctx_t;

static void emit_width(void *ctx, uint16_t token)
{
    fa_width_ctx_t *c = (fa_width_ctx_t *)ctx;
    c->width += token_width(c->font, token);
}

int menu_fa_width(const menu_fa_font_t *font, const char *text)
{
    fa_width_ctx_t c;
    if (font == NULL) return 0;
    c.font = font;
    c.width = 0;
    (void)fa_walk(text, emit_width, &c);
    return c.width;
}

/** State for menu_fa_shape(): where to store tokens and how many so far. */
typedef struct {
    uint16_t *out;
    uint16_t  max;
    uint16_t  n;
} fa_shape_ctx_t;

static void emit_store(void *ctx, uint16_t token)
{
    fa_shape_ctx_t *s = (fa_shape_ctx_t *)ctx;
    if (s->out != NULL && s->n < s->max) s->out[s->n] = token;
    s->n++;                            /* keep counting past max */
}

uint16_t menu_fa_shape(const char *text, uint16_t *out, uint16_t max)
{
    fa_shape_ctx_t s;
    s.out = out;
    s.max = max;
    s.n = 0;
    (void)fa_walk(text, emit_store, &s);
    return s.n;
}

/* ------------------------------------------------------------------------ */
/* Drawing                                                                  */
/* ------------------------------------------------------------------------ */

/** State while drawing: the pen position moves left one column at a time. */
typedef struct {
    const menu_fa_font_t *font;
    int                   x;       /* next column to draw */
    int                   x_left;  /* columns left of this are clipped */
    int                   y;       /* top row */
    bool                  invert;  /* swap foreground and background */
    menu_fa_pixel_fn      pixel;
    void                 *ctx;
} fa_draw_ctx_t;

/** Draw one column at d->x (unless clipped) and move one column left. */
static void draw_column(fa_draw_ctx_t *d, uint16_t bits)
{
    uint8_t j;
    if (d->x >= d->x_left) {
        for (j = 0; j < d->font->height; j++) {
            bool on = ((bits >> j) & 1u) != 0;
            d->pixel(d->ctx, d->x, d->y + j, on != d->invert);
        }
    }
    d->x--;
}

/** fa_walk() callback: draw every column of one token. */
static void emit_draw(void *ctx, uint16_t token)
{
    fa_draw_ctx_t *d = (fa_draw_ctx_t *)ctx;
    bool blank = (token == MENU_FA_TOK_GAP || token == MENU_FA_TOK_SPACE);
    uint8_t w = token_width(d->font, token);
    uint8_t c;

    for (c = 0; c < w; c++) {
        draw_column(d, blank ? 0u : menu_fa_glyph_column(d->font, token, c));
    }
}

int menu_fa_draw(const menu_fa_font_t *font, const char *text,
                 int x_right, int y, bool invert,
                 menu_fa_pixel_fn pixel, void *ctx)
{
    fa_draw_ctx_t d;

    if (font == NULL) return x_right;
    if (pixel == NULL) return x_right - menu_fa_width(font, text);

    d.font = font;
    d.x = x_right;
    d.x_left = 0;
    d.y = y;
    d.invert = invert;
    d.pixel = pixel;
    d.ctx = ctx;
    (void)fa_walk(text, emit_draw, &d);
    return d.x;
}

void menu_fa_draw_row(const menu_fa_font_t *font, const char *text,
                      int x_left, int x_right, int y, bool invert,
                      menu_fa_pixel_fn pixel, void *ctx)
{
    fa_draw_ctx_t d;

    if (font == NULL || pixel == NULL || x_right < x_left) return;

    d.font = font;
    d.x = x_right;
    d.x_left = x_left;
    d.y = y;
    d.invert = invert;
    d.pixel = pixel;
    d.ctx = ctx;
    (void)fa_walk(text, emit_draw, &d);

    /* Fill the rest of the row with the background colour. */
    while (d.x >= x_left) draw_column(&d, 0);
}
