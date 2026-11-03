/*
 * gfx_ext.c — primitivas extras + texto Latin-1 (acentos) + tinta de cena.
 *
 * O texto usa duas tabelas: font8x16 (ASCII) e fontext (Latin-1). Para um
 * byte >= 0x80 com letra base ASCII, desenha a letra e sobrepõe o acento
 * combinante — assim "formigaçã" sai legível sem trocar a fonte inteira.
 */
#include <string.h>
#include <stdio.h>
#include "gfx_ext.h"
#include "font8x16.h"

/* os buffers das tiras guardam RGB565 com bytes trocados (ver gfx_primitives) */
static inline uint16_t swz(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

/* ============================ primitivas ================================== */
void gfx_circle(gfx_canvas_t *c, int cx, int cy, int r, uint16_t col)
{
    if (r <= 0) { gfx_pixel(c, cx, cy, col); return; }
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        gfx_pixel(c, cx + x, cy + y, col); gfx_pixel(c, cx - x, cy + y, col);
        gfx_pixel(c, cx + x, cy - y, col); gfx_pixel(c, cx - x, cy - y, col);
        gfx_pixel(c, cx + y, cy + x, col); gfx_pixel(c, cx - y, cy + x, col);
        gfx_pixel(c, cx + y, cy - x, col); gfx_pixel(c, cx - y, cy - x, col);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void gfx_fill_circle(gfx_canvas_t *c, int cx, int cy, int r, uint16_t col)
{
    for (int dy = -r; dy <= r; dy++) {
        int dx2 = r * r - dy * dy;
        if (dx2 < 0) continue;
        /* raiz inteira */
        int dx = 0;
        while ((dx + 1) * (dx + 1) <= dx2) dx++;
        gfx_hline(c, cx - dx, cy + dy, dx * 2 + 1, col);
    }
}

void gfx_fill_ellipse(gfx_canvas_t *c, int cx, int cy, int rx, int ry, uint16_t col)
{
    if (rx <= 0 || ry <= 0) return;
    for (int dy = -ry; dy <= ry; dy++) {
        /* largura da linha pela equação da elipse, em inteiros */
        int num = (rx * rx) * (ry * ry - dy * dy);
        if (num < 0) continue;
        int dx = 0;
        while ((int64_t)(dx + 1) * (dx + 1) * ry * ry <= num) dx++;
        gfx_hline(c, cx - dx, cy + dy, dx * 2 + 1, col);
    }
}

void gfx_round_rect(gfx_canvas_t *c, int x, int y, int w, int h, int r, uint16_t col)
{
    if (r <= 0) { gfx_rect(c, x, y, w, h, col); return; }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    gfx_hline(c, x + r, y, w - 2 * r, col);
    gfx_hline(c, x + r, y + h - 1, w - 2 * r, col);
    gfx_vline(c, x, y + r, h - 2 * r, col);
    gfx_vline(c, x + w - 1, y + r, h - 2 * r, col);
    gfx_circle(c, x + r, y + r, r, col);
    gfx_circle(c, x + w - 1 - r, y + r, r, col);
    gfx_circle(c, x + r, y + h - 1 - r, r, col);
    gfx_circle(c, x + w - 1 - r, y + h - 1 - r, r, col);
}

void gfx_fill_round_rect(gfx_canvas_t *c, int x, int y, int w, int h, int r, uint16_t col)
{
    if (r <= 0) { gfx_fill(c, x, y, w, h, col); return; }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    gfx_fill(c, x + r, y, w - 2 * r, h, col);
    gfx_fill(c, x, y + r, w, h - 2 * r, col);
    gfx_fill_circle(c, x + r, y + r, r, col);
    gfx_fill_circle(c, x + w - 1 - r, y + r, r, col);
    gfx_fill_circle(c, x + r, y + h - 1 - r, r, col);
    gfx_fill_circle(c, x + w - 1 - r, y + h - 1 - r, r, col);
}

void gfx_thick_line(gfx_canvas_t *c, int x0, int y0, int x1, int y1,
                    int thick, uint16_t col)
{
    int t = thick / 2;
    if (t < 1) { gfx_line(c, x0, y0, x1, y1, col); return; }
    /* para túneis: varre em X (mais comum no cenário) com linha vertical grossa */
    int dx = x1 - x0, dy = y1 - y0;
    int steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    if (steps == 0) { gfx_fill(c, x0 - t, y0 - t, thick, thick, col); return; }
    for (int i = 0; i <= steps; i++) {
        int x = x0 + dx * i / steps;
        int y = y0 + dy * i / steps;
        gfx_vline(c, x, y - t, thick, col);
        gfx_pixel(c, x, y - t - 1, col);
    }
}

void gfx_dither(gfx_canvas_t *c, int x, int y, int w, int h, uint16_t col, int step)
{
    if (step < 2) step = 2;
    for (int yy = y; yy < y + h; yy += step) {
        int off = ((yy - y) / step) & 1;
        for (int xx = x + off * (step / 2); xx < x + w; xx += step) {
            gfx_pixel(c, xx, yy, col);
        }
    }
}

void gfx_tint(gfx_canvas_t *c, int x, int y, int w, int h, int level, uint16_t col)
{
    if (level <= 0) return;
    if (level > 100) level = 100;

    int x0 = x, y0 = y, x1 = x + w - 1, y1 = y + h - 1;
    if (x0 < c->cx0) x0 = c->cx0;
    if (y0 < c->cy0) y0 = c->cy0;
    if (x1 > c->cx1) x1 = c->cx1;
    if (y1 > c->cy1) y1 = c->cy1;
    if (x0 > x1 || y0 > y1) return;

    /* col já vem com bytes trocados; desmonta para 5:6:5 */
    uint16_t tc = col;
    int tr = (tc >> 11) & 0x1F, tg = (tc >> 5) & 0x3F, tb = tc & 0x1F;
    int k = level;

    for (int yy = y0; yy <= y1; yy++) {
        uint16_t *row = c->buf + (yy - c->y0) * c->w + (x0 - c->x0);
        for (int xx = x0; xx <= x1; xx++) {
            uint16_t p = *row;                       /* big-endian no buffer */
            int r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
            r = (r * (100 - k) + tr * k) / 100;
            g = (g * (100 - k) + tg * k) / 100;
            b = (b * (100 - k) + tb * k) / 100;
            *row++ = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }
}

/* ============================ texto Latin-1 =============================== */
static void draw_bits(gfx_canvas_t *c, int x, int y, const uint8_t *g,
                      uint16_t fgv, int scale)
{
    for (int row = 0; row < FONT8X16_H; row++) {
        uint8_t bits = g[row];
        if (!bits) continue;
        int yy = y + row * scale;
        if (yy + scale - 1 < c->cy0 || yy > c->cy1) continue;
        for (int col = 0; col < FONT8X16_W; col++) {
            if (!((bits >> (7 - col)) & 1)) continue;
            int xx = x + col * scale;
            if (xx + scale - 1 < c->cx0 || xx > c->cx1) continue;
            if (scale == 1) {
                c->buf[(yy - c->y0) * c->w + (xx - c->x0)] = fgv;
            } else {
                for (int sy = 0; sy < scale; sy++) {
                    if (yy + sy > c->cy1) break;
                    for (int sx = 0; sx < scale; sx++) {
                        if (xx + sx > c->cx1) break;
                        c->buf[(yy + sy - c->y0) * c->w + (xx + sx - c->x0)] = fgv;
                    }
                }
            }
        }
    }
}

static const uint8_t s_blank[FONT8X16_H] = { 0 };

static const uint8_t *glyph_for(unsigned char ch)
{
    if (ch >= FONT8X16_FIRST && ch <= FONT8X16_LAST) {
        return font8x16_glyphs[ch - FONT8X16_FIRST];
    }
    if (ch >= FONTEXT_FIRST) {   /* FONTEXT_LAST == 0xFF: sempre cabe em unsigned char */
        return fontext_glyphs[ch - FONTEXT_FIRST];
    }
    return s_blank;
}

void gtext(gfx_canvas_t *c, int x, int y, const char *s, uint16_t fg, int scale)
{
    uint16_t fgv = swz(fg);
    int cw = FONT8X16_W * scale;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++, x += cw) {
        if (x > c->cx1) break;
        unsigned char ch = *p;
        uint8_t base = 0, comb = 0;
        if (ch >= FONTEXT_FIRST) {   /* idem: FONTEXT_LAST == 0xFF */
            int i = ch - FONTEXT_FIRST;
            base = fontext_alias[i];
            comb = fontext_comb[i];
        }
        if (base) {
            draw_bits(c, x, y, glyph_for(base), fgv, scale);
            if (comb) {
                /* o acento fica em fontext_glyphs[comb] (índices 1..7) */
                draw_bits(c, x, y, fontext_glyphs[comb], fgv, scale);
            }
        } else {
            draw_bits(c, x, y, glyph_for(ch), fgv, scale);
        }
    }
}

int gtext_w(const char *s, int scale)
{
    return (int)strlen(s) * FONT8X16_W * scale;
}

void gtext_bg(gfx_canvas_t *c, int x, int y, const char *s, uint16_t fg,
              uint16_t bg, int scale)
{
    int w = gtext_w(s, scale);
    if (x + w - 1 >= c->cx0 && x <= c->cx1) {
        gfx_fill(c, x, y, w, FONT8X16_H * scale, bg);
    }
    gtext(c, x, y, s, fg, scale);
}

void gtext_fit(gfx_canvas_t *c, int x, int y, int max_w, const char *s,
               uint16_t fg, int scale)
{
    int cw = FONT8X16_W * scale;
    int max_chars = max_w / cw;
    if (max_chars <= 0) return;
    int n = (int)strlen(s);
    if (n <= max_chars) { gtext(c, x, y, s, fg, scale); return; }
    char tmp[64];
    int keep = max_chars - 1;
    if (keep > (int)sizeof(tmp) - 2) keep = (int)sizeof(tmp) - 2;
    if (keep < 1) keep = 1;
    memcpy(tmp, s, (size_t)keep);
    tmp[keep] = '>';
    tmp[keep + 1] = '\0';
    gtext(c, x, y, tmp, fg, scale);
}

void gtext_center(gfx_canvas_t *c, int cx, int y, const char *s, uint16_t fg, int scale)
{
    int w = gtext_w(s, scale);
    gtext(c, cx - w / 2, y, s, fg, scale);
}
