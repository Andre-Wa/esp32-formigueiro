/*
 * ui_engine.h — Engine de UI por tiras com dirty regions (herdada do
 * cyd-player e adaptada ao Formigueiro).
 *
 *  - Dois buffers parciais DMA (320x16 RGB565, 10,24 KB cada) em ping-pong;
 *  - Redesenho restrito às tiras marcadas como sujas (bitmask);
 *  - ui_dirty_rect() marca apenas as tiras cobertas por uma região, o que
 *    permite animar as formigas sem redesenhar a tela inteira;
 *  - Tarefa ui_render (Core 1, prio 6): consome a fila de comandos, roda a
 *    simulação e renderiza; tarefa touch (Core 1, prio 5): amostra o XPT2046.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "gfx_primitives.h"
#include "app_types.h"

typedef enum {
    SCREEN_SPLASH = 0,
    SCREEN_GAME,
    SCREEN_SETTINGS,
    SCREEN_CALIB,
    SCREEN_ORIENT,
    SCREEN_COUNT,
} screen_id_t;

typedef enum {
    TEV_PRESS = 0,
    TEV_RELEASE,
    TEV_TAP,
    TEV_DRAG,
    TEV_LONG,   /* pressão mantida >= 0,9 s (uma vez por toque) */
} touch_evt_type_t;

typedef struct {
    touch_evt_type_t type;
    int x, y;         /* coords de tela */
    int dx, dy;       /* incremento (DRAG) */
    uint16_t rx, ry;  /* coords brutas do ADC (calibração) */
} touch_evt_t;

typedef struct {
    const char *name;
    void (*on_enter)(void);
    void (*on_exit)(void);
    void (*draw)(gfx_canvas_t *c);              /* coords de tela; clip por tira */
    void (*on_touch)(const touch_evt_t *ev);    /* pode ser NULL */
} ui_screen_t;

/* -------- API -------- */
void ui_init(void);
void ui_show(screen_id_t id);
void ui_invalidate_all(void);
int64_t ui_now_ms(void);

/* marca as tiras cobertas pela região (coords de tela) */
void ui_dirty_rect(int x, int y, int w, int h);
void ui_dirty_all(void);

/* a tarefa do jogo chama isto a cada tick para renderizar o que está sujo */
void ui_pump(void);
