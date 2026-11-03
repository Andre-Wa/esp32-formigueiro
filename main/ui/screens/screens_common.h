/*
 * screens_common.h — elementos compartilhados pelas telas de sistema
 * (calibração, orientação, configurações).
 */
#pragma once

#include "gfx_primitives.h"
#include "gfx_ext.h"
#include "ui_engine.h"

#define HEADER_H 26

/* Barra superior: título + versão */
void ui_draw_header(gfx_canvas_t *c, const char *title);

/* Mensagem do jogo na base da tela (se houver) */
void ui_draw_toast(gfx_canvas_t *c);

/* Botão "Voltar" no cabeçalho (x 292..316, y 3..23) */
void ui_draw_back_button(gfx_canvas_t *c);
bool ui_back_button_hit(int x, int y);
