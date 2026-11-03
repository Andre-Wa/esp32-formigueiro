/*
 * render.h — Desenho das duas cenas do habitat (interior e exterior).
 * As funções desenham APENAS a área da vista (VIEW_Y..VIEW_Y+VIEW_H-1);
 * barras de status/ação e partículas são responsabilidade da game_screen.
 */
#pragma once

#include "gfx_ext.h"

void render_interior(gfx_canvas_t *c);
void render_outside(gfx_canvas_t *c);

/* céu compartilhado pelas duas cenas (faixa superior da vista) */
void render_sky(gfx_canvas_t *c, int y0, int h, bool with_ground_line);

/* util: nível de escuridão noturna 0..100 para a hora atual */
int render_night_level(void);
