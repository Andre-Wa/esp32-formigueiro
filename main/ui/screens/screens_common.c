/*
 * screens_common.c — cabeçalho/toast/voltar das telas de sistema.
 */
#include <string.h>
#include <stdio.h>
#include "screens_common.h"
#include "app_config.h"
#include "colony.h"
#include "sprites.h"

void ui_draw_header(gfx_canvas_t *c, const char *title)
{
    gfx_fill(c, 0, 0, LCD_H_RES, HEADER_H, COL_PANEL);
    gfx_hline(c, 0, HEADER_H - 1, LCD_H_RES, COL_BORDER);
    gtext_fit(c, 8, 5, 240, title, COL_TEXT, 1);
    gtext(c, 250, 5, "v" APP_VERSION, COL_MUTED, 1);
}

void ui_draw_toast(gfx_canvas_t *c)
{
    if (!g_col.msg_ticks || !g_col.msg[0]) return;
    int y = LCD_V_RES - 22;
    gfx_fill(c, 0, y, LCD_H_RES, 22, COL_UI_BG);
    gfx_hline(c, 0, y, LCD_H_RES, COL_BORDER);
    gtext_fit(c, 6, y + 3, LCD_H_RES - 12, g_col.msg, COL_UI_ACCENT, 1);
}

void ui_draw_back_button(gfx_canvas_t *c)
{
    gfx_fill_round_rect(c, 292, 3, 24, 20, 4, COL_PANEL2);
    gfx_round_rect(c, 292, 3, 24, 20, 4, COL_BORDER);
    spr_draw_centered(c, SPR_ICO_BACK, 304, 13);
}

bool ui_back_button_hit(int x, int y)
{
    return gfx_in_rect(x, y, 292, 3, 24, 20);
}
