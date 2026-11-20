/**
 * @file test_menu_fa.c
 * @brief Host-side tests for the Persian text module. Run with `make test`.
 */
#include <stdio.h>
#include <string.h>
#include "menu_fa.h"

static int failures = 0;

#define CHECK(cond)                                                  \
    do {                                                             \
        if (!(cond)) {                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            failures++;                                              \
        }                                                            \
    } while (0)

/* ---- Short names for the expected tokens ---- */
#define GAP    MENU_FA_TOK_GAP
#define SPACE  MENU_FA_TOK_SPACE
#define ISO    MENU_FA_FORM_ISOLATED
#define FIN    MENU_FA_FORM_FINAL
#define MED    MENU_FA_FORM_MEDIAL
#define INI    MENU_FA_FORM_INITIAL
#define L(letter, form) MENU_FA_SLOT_LETTER(letter, form)
#define D(n)   (MENU_FA_SLOT_DIGIT0 + (n))          /* Persian digit */
#define A(c)   (MENU_FA_SLOT_ASCII + ((c) - 0x20))  /* ASCII glyph */

/* Letter indices (see fa_letters in menu_fa.c). */
enum { HAMZA = 0, ALEF_MADDA = 1, ALEF = 2, BE = 3, JIM = 7, DAL = 11, SIN = 16,
       SHIN = 17, EYN = 22, KAF = 26, LAM = 28, MIM = 29, VAV = 31, HE = 32, YE = 33 };

/* Shape `text` and compare with the expected tokens. */
static int shaped_as(const char *text, const uint16_t *expect, uint16_t n)
{
    uint16_t out[64];
    uint16_t got = menu_fa_shape(text, out, 64);
    uint16_t i;

    if (got != n) {
        printf("  \"%s\": %u tokens, expected %u:", text, got, n);
        for (i = 0; i < got && i < 64; i++) printf(" %u", out[i]);
        printf("\n");
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (out[i] != expect[i]) {
            printf("  \"%s\": token %u is %u, expected %u\n", text, i, out[i], expect[i]);
            return 0;
        }
    }
    return 1;
}

#define SHAPED_AS(text, ...)                                         \
    do {                                                             \
        static const uint16_t e_[] = { __VA_ARGS__ };                \
        CHECK(shaped_as(text, e_, (uint16_t)(sizeof(e_) / sizeof(e_[0])))); \
    } while (0)

/* ------------------------------------------------------------------------ */

static void test_forms(void)
{
    SHAPED_AS("ب", GAP, L(BE, ISO));
    SHAPED_AS("سب", GAP, L(SIN, INI), L(BE, FIN));

    /* Four forms of ع. */
    SHAPED_AS("ع",   GAP, L(EYN, ISO));
    SHAPED_AS("عی",  GAP, L(EYN, INI), L(YE, FIN));
    SHAPED_AS("بعد", GAP, L(BE, INI), L(EYN, MED), L(DAL, FIN));
    SHAPED_AS("بع",  GAP, L(BE, INI), L(EYN, FIN));

    /* Right-joining letters never connect forward. */
    SHAPED_AS("دب", GAP, L(DAL, ISO), GAP, L(BE, ISO));

    /* ه has four distinct forms. */
    SHAPED_AS("ه",   GAP, L(HE, ISO));
    SHAPED_AS("هم",  GAP, L(HE, INI), L(MIM, FIN));
    SHAPED_AS("بهم", GAP, L(BE, INI), L(HE, MED), L(MIM, FIN));
    SHAPED_AS("به",  GAP, L(BE, INI), L(HE, FIN));

    SHAPED_AS("آب", GAP, L(ALEF_MADDA, ISO), GAP, L(BE, ISO));
    SHAPED_AS("ء",  GAP, L(HAMZA, ISO));
    SHAPED_AS("بء", GAP, L(BE, ISO), GAP, L(HAMZA, ISO));   /* ء never joins */
    SHAPED_AS("؟",  GAP, MENU_FA_SLOT_QUESTION);
}

static void test_lam_alef(void)
{
    SHAPED_AS("لا",   GAP, MENU_FA_SLOT_LAMALEF);
    /* سلام: س initial, لا final (joined to س), then م isolated. */
    SHAPED_AS("سلام", GAP, L(SIN, INI), MENU_FA_SLOT_LAMALEF_FINAL, GAP, L(MIM, ISO));
    /* Every alef forms the ligature. */
    SHAPED_AS("لآ", GAP, MENU_FA_SLOT_LAMALEF);
    SHAPED_AS("لأ", GAP, MENU_FA_SLOT_LAMALEF);
    /* ل before another letter is not a ligature. */
    SHAPED_AS("لب", GAP, L(LAM, INI), L(BE, FIN));
    /* A half-space between them prevents the ligature. */
    SHAPED_AS("ل\xE2\x80\x8C" "ا", GAP, L(LAM, ISO), GAP, L(ALEF, ISO));
}

static void test_spaces_and_halfspace(void)
{
    SHAPED_AS("ب ب", GAP, L(BE, ISO), SPACE, GAP, L(BE, ISO));
    SHAPED_AS("ب\xC2\xA0ب", GAP, L(BE, ISO), SPACE, GAP, L(BE, ISO));   /* NBSP */

    /* می‌شود: م initial, ی final | ش initial, و final, د isolated. */
    SHAPED_AS("می\xE2\x80\x8Cشود", GAP, L(MIM, INI), L(YE, FIN),
              GAP, L(SHIN, INI), L(VAV, FIN), GAP, L(DAL, ISO));
    /* Without the half-space, ی joins forward. */
    SHAPED_AS("میش", GAP, L(MIM, INI), L(YE, MED), L(SHIN, FIN));
}

static void test_numbers(void)
{
    /* Persian digits, number drawn back to front: "70" reads ۷۰. */
    SHAPED_AS("70",   GAP, D(0), GAP, D(7));
    SHAPED_AS("۱۴۰۵", GAP, D(5), GAP, D(0), GAP, D(4), GAP, D(1));
    SHAPED_AS("\xD9\xA3", GAP, D(3));                        /* Arabic-Indic ٣ */
    SHAPED_AS("ب 12", GAP, L(BE, ISO), SPACE, GAP, D(2), GAP, D(1));
    SHAPED_AS("ب1ب",  GAP, L(BE, ISO), GAP, D(1), GAP, L(BE, ISO));

    /* Separators between digits stay inside the number. */
    SHAPED_AS("12:30", GAP, D(0), GAP, D(3), GAP, A(':'), GAP, D(2), GAP, D(1));
    SHAPED_AS("3.5",   GAP, D(5), GAP, A('.'), GAP, D(3));
    SHAPED_AS("1,000", GAP, D(0), GAP, D(0), GAP, D(0), GAP, A(','), GAP, D(1));
    SHAPED_AS("۳٫۵",   GAP, D(5), GAP, MENU_FA_SLOT_DECIMAL, GAP, D(3));

    /* Separator not followed by a digit, or before the number: outside it. */
    SHAPED_AS("12.",  GAP, D(2), GAP, D(1), GAP, A('.'));
    SHAPED_AS("-5",   GAP, A('-'), GAP, D(5));
    SHAPED_AS("1::2", GAP, D(1), GAP, A(':'), GAP, A(':'), GAP, D(2));

    /* Percent after a number belongs to it. */
    SHAPED_AS("50%",  GAP, A('%'), GAP, D(0), GAP, D(5));
    SHAPED_AS("۵۰٪",  GAP, MENU_FA_SLOT_PERCENT, GAP, D(0), GAP, D(5));
}

static void test_latin(void)
{
    /* A Latin word is drawn back to front with ASCII glyphs. */
    SHAPED_AS("WiFi", GAP, A('i'), GAP, A('F'), GAP, A('i'), GAP, A('W'));

    /* Mixed: the Latin run ends before the space that precedes Persian. */
    SHAPED_AS("WiFi ب", GAP, A('i'), GAP, A('F'), GAP, A('i'), GAP, A('W'),
              SPACE, GAP, L(BE, ISO));
    SHAPED_AS("ب Ok", GAP, L(BE, ISO), SPACE, GAP, A('k'), GAP, A('O'));

    /* Spaces, punctuation and digits inside a Latin run are part of it,
     * and digits there use ASCII glyphs. */
    SHAPED_AS("USB 2.0", GAP, A('0'), GAP, A('.'), GAP, A('2'), SPACE,
              GAP, A('B'), GAP, A('S'), GAP, A('U'));
    SHAPED_AS("Wi-Fi", GAP, A('i'), GAP, A('F'), GAP, A('-'), GAP, A('i'), GAP, A('W'));

    /* A number right before a Latin letter joins the Latin run. */
    SHAPED_AS("2.4GHz", GAP, A('z'), GAP, A('H'), GAP, A('G'), GAP, A('4'),
              GAP, A('.'), GAP, A('2'));

    /* A closing bracket matching one opened inside the run stays with it. */
    SHAPED_AS("A (B)", GAP, A(')'), GAP, A('B'), GAP, A('('), SPACE, GAP, A('A'));
    SHAPED_AS("ب A(1).", GAP, L(BE, ISO), SPACE, GAP, A(')'), GAP, A('1'), GAP, A('('),
              GAP, A('A'), GAP, A('.'));
    /* ...but not one that was opened before the run (it's right-to-left). */
    SHAPED_AS("(A)", GAP, A(')'), GAP, A('A'), GAP, A('('));

    /* Latin letters break Persian joining. */
    SHAPED_AS("بxب", GAP, L(BE, ISO), GAP, A('x'), GAP, L(BE, ISO));
}

static void test_mirroring(void)
{
    /* In right-to-left text brackets are mirrored: "(" draws as ")". */
    SHAPED_AS("(ج)", GAP, A(')'), GAP, L(JIM, ISO), GAP, A('('));
    SHAPED_AS("[ب]", GAP, A(']'), GAP, L(BE, ISO), GAP, A('['));
    SHAPED_AS("«ب»", GAP, MENU_FA_SLOT_QUOTE_R, GAP, L(BE, ISO), GAP, MENU_FA_SLOT_QUOTE_L);
    /* Not inside a Latin run. */
    SHAPED_AS("A (B) C", GAP, A('C'), SPACE, GAP, A(')'), GAP, A('B'), GAP, A('('),
              SPACE, GAP, A('A'));
}

static void test_variants_and_bad_input(void)
{
    uint16_t a[16], b[16];
    uint16_t na = menu_fa_shape("كي", a, 16);   /* Arabic kaf, ye */
    uint16_t nb = menu_fa_shape("کی", b, 16);   /* Persian kaf, ye */
    CHECK(na == nb && memcmp(a, b, na * sizeof(a[0])) == 0);

    /* Characters no font has are skipped. */
    SHAPED_AS("ب\xF0\x9F\x98\x80ب", GAP, L(BE, ISO), GAP, L(BE, ISO));   /* emoji */

    /* Malformed UTF-8 is skipped without running past the end. */
    CHECK(menu_fa_shape("\xD8", NULL, 0) == 0);
    CHECK(menu_fa_shape("\xE2\x80", NULL, 0) == 0);
    SHAPED_AS("\x80\xFF" "ب", GAP, L(BE, ISO));

    CHECK(menu_fa_shape("", NULL, 0) == 0);
    CHECK(menu_fa_shape(NULL, NULL, 0) == 0);
}

static void test_shape_truncation(void)
{
    uint16_t out[3] = { 0xAAAA, 0xAAAA, 0xAAAA };
    /* سلام needs 5 tokens; only 2 fit. */
    CHECK(menu_fa_shape("سلام", out, 2) == 5);
    CHECK(out[0] == GAP && out[1] == L(SIN, INI) && out[2] == 0xAAAA);
}

/* Every slot that shaping can produce must have a glyph in both fonts. */
static void test_font_coverage(const menu_fa_font_t *f, const char *name)
{
    uint16_t s;
    int missing = 0;
    for (s = 0; s < MENU_FA_SLOT_COUNT; s++) {
        bool used = (s < MENU_FA_SLOT_DIGIT0 + 10u) ||
                    (s >= MENU_FA_SLOT_QUESTION && s <= MENU_FA_SLOT_LAMALEF_FINAL) ||
                    (s > MENU_FA_SLOT_ASCII);
        if (used && menu_fa_glyph_width(f, s) == 0) {
            printf("  %s: slot %u has no glyph\n", name, s);
            missing++;
        }
    }
    CHECK(missing == 0);
    CHECK(menu_fa_glyph_width(f, MENU_FA_SLOT_COUNT) == 0);
    CHECK(menu_fa_glyph_width(NULL, 0) == 0);
    CHECK(menu_fa_glyph_column(f, L(BE, ISO), 200) == 0);
}

static void test_widths(void)
{
    const menu_fa_font_t *f = &menu_fa_font8;
    CHECK(menu_fa_glyph_width(f, L(ALEF, FIN)) == 1);   /* ا final: one column */
    CHECK(menu_fa_glyph_width(f, L(BE, ISO)) == 8);     /* ب: full width */
    CHECK(menu_fa_width(f, "") == 0);
    CHECK(menu_fa_width(f, NULL) == 0);
    CHECK(menu_fa_width(NULL, "ب") == 0);
    CHECK(menu_fa_width(f, " ") == f->space_width);
    CHECK(menu_fa_width(f, "ب") == f->gap_width + 8);
    CHECK(menu_fa_width(f, "ب ب") == 2 * (f->gap_width + 8) + f->space_width);

    /* The 16 px font is wider. */
    CHECK(menu_fa_width(&menu_fa_font16, "سلام") > menu_fa_width(f, "سلام"));
}

/* ---- Drawing into a small framebuffer ---- */
#define W 60
#define H 20
static signed char fb[H][W];   /* -1 = untouched, 0 = off, 1 = on */
static int out_of_bounds = 0;

static void px(void *ctx, int x, int y, bool on)
{
    (void)ctx;
    if (x < 0 || x >= W || y < 0 || y >= H) { out_of_bounds++; return; }
    fb[y][x] = on ? 1 : 0;
}

static void fb_reset(void) { memset(fb, -1, sizeof(fb)); out_of_bounds = 0; }

static int column_touched(int x, int height)
{
    int y;
    for (y = 0; y < height; y++) if (fb[y][x] != -1) return 1;
    return 0;
}

static void test_draw(const menu_fa_font_t *f)
{
    int x, y, left, ons = 0, ons_inv = 0;
    int gw = f->gap_width;
    int bw = menu_fa_glyph_width(f, L(BE, ISO));
    uint8_t c;

    /* ب at the right edge: gap + glyph columns, nothing else. */
    fb_reset();
    left = menu_fa_draw(f, "ب", 50, 0, false, px, NULL);
    CHECK(left == 50 - gw - bw);
    for (x = 0; x < W; x++) CHECK(column_touched(x, f->height) == (x > left && x <= 50));
    for (y = 0; y < f->height; y++) CHECK(fb[y][50] == 0);     /* the gap */
    for (c = 0; c < bw; c++) {
        for (y = 0; y < f->height; y++) {
            int expect = (menu_fa_glyph_column(f, L(BE, ISO), c) >> y) & 1;
            CHECK(fb[y][50 - gw - c] == expect);
        }
    }

    /* Inverted: every pixel flips. */
    for (y = 0; y < f->height; y++) for (x = left + 1; x <= 50; x++) ons += fb[y][x];
    fb_reset();
    menu_fa_draw(f, "ب", 50, 0, true, px, NULL);
    for (y = 0; y < f->height; y++) for (x = left + 1; x <= 50; x++) ons_inv += fb[y][x];
    CHECK(ons + ons_inv == (50 - left) * f->height);

    /* Nothing drawn left of x = 0 or outside the font height. */
    fb_reset();
    menu_fa_draw(f, "سلام WiFi سلام", 5, 0, false, px, NULL);
    CHECK(out_of_bounds == 0);
    for (x = 0; x < W; x++) for (y = f->height; y < H; y++) CHECK(fb[y][x] == -1);

    /* NULL pixel function just measures; NULL font draws nothing. */
    CHECK(menu_fa_draw(f, "ب", 20, 0, false, NULL, NULL) == 20 - gw - bw);
    CHECK(menu_fa_draw(NULL, "ب", 20, 0, false, px, NULL) == 20);
}

static void test_draw_row(const menu_fa_font_t *f)
{
    int x, y;

    /* Row from 5 to 54: text on the right, the rest filled, nothing outside. */
    fb_reset();
    menu_fa_draw_row(f, "ب", 5, 54, 1, true, px, NULL);
    for (x = 0; x < W; x++) {
        int inside = (x >= 5 && x <= 54);
        for (y = 1; y < 1 + f->height; y++) CHECK((fb[y][x] != -1) == inside);
    }
    for (y = 1; y < 1 + f->height; y++) CHECK(fb[y][5] == 1);   /* inverted background */

    /* Longer than the row: clipped at x_left. */
    fb_reset();
    menu_fa_draw_row(f, "سلام سلام سلام سلام", 10, 20, 0, false, px, NULL);
    for (x = 0; x < W; x++) CHECK(column_touched(x, f->height) == (x >= 10 && x <= 20));

    /* Bad range or NULLs: nothing happens. */
    fb_reset();
    menu_fa_draw_row(f, "ب", 20, 10, 0, false, px, NULL);
    menu_fa_draw_row(NULL, "ب", 0, 10, 0, false, px, NULL);
    menu_fa_draw_row(f, "ب", 0, 10, 0, false, NULL, NULL);
    for (x = 0; x < W; x++) CHECK(!column_touched(x, H));
}

int main(void)
{
    test_forms();
    test_lam_alef();
    test_spaces_and_halfspace();
    test_numbers();
    test_latin();
    test_mirroring();
    test_variants_and_bad_input();
    test_shape_truncation();
    test_font_coverage(&menu_fa_font8, "font8");
    test_font_coverage(&menu_fa_font16, "font16");
    test_widths();
    test_draw(&menu_fa_font8);
    test_draw(&menu_fa_font16);
    test_draw_row(&menu_fa_font8);
    test_draw_row(&menu_fa_font16);

    if (failures == 0) {
        printf("All Persian tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", failures);
    return 1;
}
