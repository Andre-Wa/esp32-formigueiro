/*
 * sprites.h — Sprites pixel-art do jogo (índice -> arte em flash).
 *
 * ARTE GERADA AUTOMATICAMENTE por tools/gen_sprites.py — não edite o enum
 * manualmente sem atualizar o gerador (a ordem precisa bater).
 *
 * Formato: 1 byte por pixel = índice na paleta; 0 = transparente.
 */
#pragma once

#include <stdint.h>
#include "gfx_ext.h"

typedef struct {
    uint8_t        w;
    uint8_t        h;
    const uint16_t *pal;    /* pal[i] = cor RGB565 já com bytes trocados */
    const uint8_t *px;      /* w*h bytes */
} sprite_t;

/* --- enum gerado --- */
typedef enum {
    SPR_ANT_WALK_R = 0,
    SPR_ANT_WALK_L,
    SPR_ANT_CARRY_R,
    SPR_ANT_CARRY_L,
    SPR_ANT_WALK2_R,
    SPR_ANT_WALK2_L,
    SPR_ANT_SLEEP,
    SPR_ANT_EAT,
    SPR_QUEEN_IDLE,
    SPR_QUEEN_WALK,
    SPR_QUEEN_HAPPY,
    SPR_QUEEN_SLEEP,
    SPR_CROWN,
    SPR_HEART,
    SPR_ZZZ,
    SPR_SPARK,
    SPR_LEAF,
    SPR_LEAF_PILE,
    SPR_BERRY,
    SPR_BUSH,
    SPR_FLOWER_R,
    SPR_FLOWER_Y,
    SPR_SEEDS,
    SPR_TWIG,
    SPR_MUSHROOM,
    SPR_EGG,
    SPR_LARVA,
    SPR_PUPA,
    SPR_BEETLE,
    SPR_SPIDER,
    SPR_PILE_FOOD,
    SPR_PILE_MAT,
    SPR_THRONE,
    SPR_MOUND,
    SPR_GRASS,
    SPR_PEBBLE,
    SPR_SUN,
    SPR_CLOUD,
    SPR_MOON,
    SPR_ICO_FEED,
    SPR_ICO_PLAY,
    SPR_ICO_HUNT,
    SPR_ICO_REST,
    SPR_ICO_WORK,
    SPR_ICO_PET,
    SPR_ICO_CLEAN,
    SPR_ICO_EGG,
    SPR_ICO_HOME,
    SPR_ICO_TREE,
    SPR_ICO_GEAR,
    SPR_ICO_BACK,
    SPR_ICO_CLOCK,
    SPR_ICO_WIFI,
    SPR_ICO_RAIN,
    SPR_ICO_STORM,
    SPR_ICO_SNOW,
    SPR_ICO_FOG,
    SPR_ICO_DROP,
    SPR_ICO_LEAF,
    SPR_ICO_MAT,
    SPR_COUNT
} sprite_id_t;

extern const sprite_t g_sprites[SPR_COUNT];

/* Desenha o sprite com o canto superior esquerdo em (x,y), com recorte. */
void spr_draw(gfx_canvas_t *c, sprite_id_t id, int x, int y);

/* idem, espelhado horizontalmente. */
void spr_draw_flip(gfx_canvas_t *c, sprite_id_t id, int x, int y);

/* Desenha centrado em (cx,cy). */
void spr_draw_centered(gfx_canvas_t *c, sprite_id_t id, int cx, int cy);

/* Teste de toque: o ponto está dentro da caixa do sprite centrado? */
bool spr_hit(sprite_id_t id, int cx, int cy, int px, int py, int pad);

/* Dimensões */
static inline int spr_w(sprite_id_t id) { return g_sprites[id].w; }
static inline int spr_h(sprite_id_t id) { return g_sprites[id].h; }
