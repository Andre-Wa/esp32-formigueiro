/*
 * gfx_ext.h — Extensões do conjunto de primitivas herdado do cyd-player:
 *
 *  - GFX_RGB(): cor RGB565 já com os bytes trocados (ordem que o ILI9341
 *    espera no RAMWR). As primitivas gfx_* continuam aceitando a ordem
 *    "normal" (elas trocam internamente); os sprites usam GFX_RGB direto.
 *  - Fonte estendida: glifos acentuados (Latin-1) que a font8x16 ASCII não
 *    cobre, desenhados automaticamente pelo gfx_text().
 *  - Círculo/ellipse/elipse preenchida, arco, dithering e retângulos
 *    arredondados, úteis para o cenário orgânico do formigueiro.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "gfx_primitives.h"

/* GFX_RGB já vem de gfx_primitives.h; aqui só reaproveitamos o nome. */

/* ---------------- paleta do tema "terra/formigueiro" ---------------- */
#define COL_SKY_DAY    GFX_RGB(110, 178, 232)
#define COL_SKY_DUSK   GFX_RGB(214, 128,  74)
#define COL_SKY_NIGHT  GFX_RGB( 16,  22,  48)
#define COL_SOIL_DARK  GFX_RGB( 46,  28,  18)
#define COL_SOIL       GFX_RGB( 78,  50,  30)
#define COL_SOIL_LIGHT GFX_RGB(116,  78,  46)
#define COL_TUNNEL     GFX_RGB( 30,  18,  12)
#define COL_TUNNEL_HI  GFX_RGB( 58,  36,  24)
#define COL_GRASS      GFX_RGB( 76, 148,  58)
#define COL_GRASS_DK   GFX_RGB( 44,  96,  38)
#define COL_LEAF       GFX_RGB( 96, 176,  64)
#define COL_ANT_BODY   GFX_RGB( 42,  26,  20)
#define COL_ANT_HI     GFX_RGB( 92,  60,  40)
#define COL_QUEEN_BODY GFX_RGB( 62,  34,  26)
#define COL_QUEEN_HI   GFX_RGB(146,  92,  54)
#define COL_GOLD       GFX_RGB(255, 208,  64)
#define COL_HONEY      GFX_RGB(226, 168,  60)
#define COL_EGG        GFX_RGB(246, 240, 222)
#define COL_HEART      GFX_RGB(255,  96, 128)
#define COL_WATER      GFX_RGB( 96, 168, 232)
#define COL_DANGER     GFX_RGB(228,  72,  60)

/* UI */
#define COL_UI_BG      GFX_RGB( 18,  14,  12)
#define COL_UI_PANEL   GFX_RGB( 40,  30,  24)
#define COL_UI_PANEL2  GFX_RGB( 58,  44,  34)
#define COL_UI_BORDER  GFX_RGB( 96,  74,  54)
#define COL_UI_TEXT    GFX_RGB(240, 234, 220)
#define COL_UI_MUTED   GFX_RGB(164, 148, 128)
#define COL_UI_ACCENT  GFX_RGB(255, 196,  48)
#define COL_UI_OK      GFX_RGB(104, 200, 108)
#define COL_UI_WARN    GFX_RGB(240, 168,  56)
#define COL_UI_BAD     GFX_RGB(232,  88,  72)
#define COL_UI_INFO    GFX_RGB(112, 176, 232)

/* ---------------- primitivas extras ---------------- */
void gfx_circle(gfx_canvas_t *c, int cx, int cy, int r, uint16_t col);
void gfx_fill_circle(gfx_canvas_t *c, int cx, int cy, int r, uint16_t col);
void gfx_fill_ellipse(gfx_canvas_t *c, int cx, int cy, int rx, int ry, uint16_t col);
void gfx_round_rect(gfx_canvas_t *c, int x, int y, int w, int h, int r, uint16_t col);
void gfx_fill_round_rect(gfx_canvas_t *c, int x, int y, int w, int h, int r, uint16_t col);
/* linha grossa (espessura ímpar centrada) — usada nos túneis */
void gfx_thick_line(gfx_canvas_t *c, int x0, int y0, int x1, int y1,
                    int thick, uint16_t col);
/* "tinta" da cena: escurece/colore a área desenhada (noite/chuva).
 * level 0..100 (0 = sem efeito), col = cor do filtro. */
void gfx_tint(gfx_canvas_t *c, int x, int y, int w, int h, int level, uint16_t col);
/* padrão xadrez pontilhado (dither) para texturas baratas */
void gfx_dither(gfx_canvas_t *c, int x, int y, int w, int h, uint16_t col, int step);

/* ---------------- fonte estendida (Latin-1) ---------------- */
/* O gfx_text() original só cobre ASCII 32..126. Para os acentos do
 * português usamos uma segunda tabela; gfx_ext instala um "shim" que
 * intercepta os bytes >= 0x80. Chamadas com bytes < 0x80 têm custo zero. */
#include "fontext.h"   /* FONTEXT_FIRST/LAST/NUM, fontext_glyphs, alias, comb */

/* Desenha texto aceitando Latin-1 (acentos) — use SEMPRE esta no jogo. */
void gtext(gfx_canvas_t *c, int x, int y, const char *s, uint16_t fg, int scale);
void gtext_bg(gfx_canvas_t *c, int x, int y, const char *s, uint16_t fg,
              uint16_t bg, int scale);
int  gtext_w(const char *s, int scale);
void gtext_fit(gfx_canvas_t *c, int x, int y, int max_w, const char *s,
               uint16_t fg, int scale);
void gtext_center(gfx_canvas_t *c, int cx, int y, const char *s, uint16_t fg,
                  int scale);

/* ---------------- retângulos sujos (redesenho parcial) ---------------- */
/* A engine de UI trabalha com tiras de 16 px. Marcar só a região que mudou
 * economiza ~70% do bandwidth de SPI quando apenas as formigas se movem. */
void ui_dirty_rect(int x, int y, int w, int h);
void ui_dirty_all(void);
