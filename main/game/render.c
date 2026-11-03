/*
 * render.c — cenas do habitat (interior do formigueiro e mundo exterior).
 *
 * Coordenadas locais da vista: (0,0)..(319,177); a tela soma VIEW_Y.
 * Tudo é desenhado com clip da tira atual, então pode chamar sem medo:
 * o que estiver fora da tira em renderização é descartado pelo canvas.
 */
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "render.h"
#include "colony.h"
#include "sprites.h"
#include "weather.h"

#define SY(y) ((y) + VIEW_Y)

/* ============================ helpers ==================================== */
static inline uint16_t mixc(uint16_t a, uint16_t b, int t)   /* t 0..256 */
{
    /* a,b já vêm com bytes trocados: desmonta 5:6:5 */
    int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
    int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
    int r = (ar * (256 - t) + br * t) >> 8;
    int g = (ag * (256 - t) + bg * t) >> 8;
    int bl = (ab * (256 - t) + bb * t) >> 8;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

int render_night_level(void)
{
    uint8_t h = colony_hour();
    uint8_t m = colony_minute();
    int v = h * 60 + m;                 /* minutos do dia */
    /* escuro 21:00..05:00, transições 05-07 e 18-21 */
    if (v >= 21 * 60 || v < 5 * 60) return 55;
    if (v < 7 * 60)  return 55 - (v - 5 * 60) * 55 / 120;
    if (v >= 18 * 60) return (v - 18 * 60) * 55 / 180;
    return 0;
}

static uint16_t sky_top(void)
{
    int n = render_night_level();
    uint8_t h = colony_hour();
    uint16_t day = COL_SKY_DAY, night = COL_SKY_NIGHT;
    if (h >= 17 && h < 21) {
        /* crepúsculo alaranjado antes de escurecer */
        int t = (h - 17) * 60 + colony_minute();
        day = mixc(COL_SKY_DAY, COL_SKY_DUSK, t * 256 / 240);
    }
    return mixc(day, night, n * 256 / 60);
}

static uint16_t sky_bot(void)
{
    return mixc(sky_top(), GFX_RGB(210, 226, 240), 90);
}

void render_sky(gfx_canvas_t *c, int y0, int h, bool ground_line)
{
    uint16_t top = sky_top(), bot = sky_bot();
    for (int i = 0; i < h; i++) {
        gfx_hline(c, 0, y0 + i, VIEW_W, mixc(top, bot, i * 256 / (h ? h : 1)));
    }

    uint8_t hr = colony_hour();
    int mn = colony_minute();

    /* sol / lua em arco */
    if (hr >= 6 && hr < 18) {
        float p = (hr - 6 + mn / 60.0f) / 12.0f;
        int x = (int)(16 + p * 288);
        int y = y0 + 4 + (int)((1.0f - sinf(3.14159f * p)) * 10);
        spr_draw_centered(c, SPR_SUN, x, y + 6);
    } else {
        float p;
        if (hr >= 18) p = (hr - 18 + mn / 60.0f) / 12.0f;
        else p = (hr + 6 + mn / 60.0f) / 12.0f;
        int x = (int)(16 + p * 288);
        int y = y0 + 4 + (int)((1.0f - sinf(3.14159f * p)) * 8);
        spr_draw_centered(c, SPR_MOON, x, y + 6);
        /* estrelas */
        if (render_night_level() > 25) {
            for (int i = 0; i < 14; i++) {
                int sx = (i * 67 + 13) % VIEW_W;
                int sy = y0 + 2 + (i * 37) % (h > 8 ? h - 8 : 1);
                if (((g_col.total_ticks / 12 + i) % 5) != 0) continue;
                gfx_pixel(c, sx, sy, COL_EGG);
            }
        }
    }

    /* nuvens derivando */
    int drift = (int)(g_col.sec_of_day / 30);
    for (int i = 0; i < 2; i++) {
        int cx = (drift + i * 170) % (VIEW_W + 60) - 30;
        int cy = y0 + 5 + i * 9;
        spr_draw(c, SPR_CLOUD, cx, cy);
        if (colony_raining()) {
            /* nuvem de chuva: base cinza */
            gfx_fill(c, cx + 2, cy + 6, 22, 4, COL_UI_MUTED);
        }
    }
    (void)ground_line;
}

/* textura de terra determinística (sem RNG: não muda entre frames) */
static void soil_texture(gfx_canvas_t *c, int x, int y, int w, int h)
{
    for (int yy = y; yy < y + h; yy += 3) {
        for (int xx = x + ((yy / 3) & 1); xx < x + w; xx += 4) {
            uint32_t hsh = (uint32_t)(xx * 73856093u) ^ (uint32_t)(yy * 19349663u);
            uint32_t v = (hsh >> 7) & 15;
            if (v == 3) gfx_pixel(c, xx, yy, COL_SOIL_LIGHT);
            else if (v == 9) gfx_pixel(c, xx, yy, COL_SOIL_DARK);
        }
    }
}

/* ============================ interior ==================================== */
typedef struct { int a, b, t; } corridor_t;
static const corridor_t CORR[] = {   /* MESMAS arestas do ADJ em colony.c */
    { 7, 0, 12 }, { 0, 5, 12 }, { 5, 1, 12 }, { 5, 2, 12 }, { 5, 6, 9 },
    { 1, 3, 12 }, { 3, 4, 12 }, { 2, 4, 12 }, { 6, 2, 9 },
    { 3, 5, 10 }, { 4, 5, 10 }, { 1, 4, 9 },
};

static bool worker_outside(const worker_t *w)
{
    switch (w->state) {
    case W_GO_RES: case W_COLLECT: case W_HUNT: case W_RETURN:
    case W_SCOUT: case W_STUCK: case W_FLEE:
        return true;
    default:
        return false;
    }
}

static void draw_worker(gfx_canvas_t *c, const worker_t *w, int i)
{
    int x = UNQ8(w->x), y = SY(UNQ8(w->y));
    sprite_id_t id;
    bool flip = w->face >= 0;   /* arte base olha p/ esquerda */
    if (w->state == W_SLEEP) id = SPR_ANT_SLEEP;
    else if (w->state == W_EAT) id = SPR_ANT_EAT;
    else if (w->carry) id = SPR_ANT_CARRY_R;
    else id = ((g_col.total_ticks / 6 + (uint32_t)i) & 1) ? SPR_ANT_WALK_R : SPR_ANT_WALK2_R;

    int sw = spr_w(id), sh = spr_h(id);
    if (flip) spr_draw_flip(c, id, x - sw / 2, y - sh + 3);
    else spr_draw(c, id, x - sw / 2, y - sh + 3);

    if (w->hurt) {
        gfx_pixel(c, x, y - sh - 1, COL_DANGER);
        gfx_pixel(c, x - 1, y - sh, COL_DANGER);
        gfx_pixel(c, x + 1, y - sh, COL_DANGER);
        gfx_pixel(c, x, y - sh + 1, COL_DANGER);
    }
    if (g_col.sel_worker == (uint8_t)(i + 1)) {
        uint16_t col = ((g_col.total_ticks / 8) & 1) ? COL_GOLD : COL_UI_ACCENT;
        gfx_hline(c, x - 10, y - sh - 2, 5, col);
        gfx_vline(c, x - 10, y - sh - 2, 5, col);
        gfx_hline(c, x + 6, y - sh - 2, 5, col);
        gfx_vline(c, x + 10, y - sh - 2, 5, col);
        gfx_hline(c, x - 10, y + 2, 5, col);
        gfx_vline(c, x - 10, y - 2, 5, col);
        gfx_hline(c, x + 6, y + 2, 5, col);
        gfx_vline(c, x + 10, y - 2, 5, col);
    }
}

static void draw_queen(gfx_canvas_t *c)
{
    const queen_t *q = &g_col.queen;
    int x = UNQ8(q->x), y = SY(UNQ8(q->y));
    sprite_id_t id;
    bool flip = q->face >= 0;   /* arte base olha p/ esquerda */
    switch (q->state) {
    case Q_SLEEP: id = SPR_QUEEN_SLEEP; break;
    case Q_PET:
    case Q_PLAY:  id = SPR_QUEEN_HAPPY; break;
    case Q_WALK:
    case Q_TO_NURSERY:
        id = ((g_col.total_ticks / 7) & 1) ? SPR_QUEEN_WALK : SPR_QUEEN_IDLE;
        break;
    default:
        id = (q->mood > 75) ? SPR_QUEEN_HAPPY : SPR_QUEEN_IDLE;
        break;
    }
    int sw = spr_w(id), sh = spr_h(id);
    if (flip) spr_draw_flip(c, id, x - sw / 2, y - sh + 3);
    else spr_draw(c, id, x - sw / 2, y - sh + 3);

    /* brilho de vínculo alto */
    if (q->bond > 70 && ((g_col.total_ticks / 10) & 1)) {
        spr_draw_centered(c, SPR_SPARK, x + sw / 2 + 2, y - sh - 2);
    }

    /* balão de fala */
    if (q->say_timer && q->say[0]) {
        int tw = gtext_w(q->say, 1);
        int bw = tw + 8, bh = 14;
        int bx = x - bw / 2;
        if (bx < 2) bx = 2;
        if (bx + bw > VIEW_W - 2) bx = VIEW_W - 2 - bw;
        int by = y - sh - bh - 4;
        if (by < VIEW_Y + 2) by = VIEW_Y + 2;
        gfx_fill_round_rect(c, bx, by, bw, bh, 4, COL_EGG);
        gfx_rect(c, bx, by, bw, bh, COL_UI_BORDER);
        gfx_fill_tri(c, x - 2, by + bh, x + 2, by + bh, x, by + bh + 3, COL_EGG);
        gtext(c, bx + 4, by - 1, q->say, COL_UI_BG, 1);
    }
}

static void draw_chamber_content(gfx_canvas_t *c, const chamber_t *ch)
{
    int x = ch->x, y = SY(ch->y), w = ch->w, h = ch->h;
    switch (ch->kind) {
    case CH_STORAGE: {
        int fp = g_col.food / 40;  if (fp > 3) fp = 3;
        int mp = g_col.material / 30; if (mp > 3) mp = 3;
        for (int i = 0; i < fp; i++) spr_draw(c, SPR_PILE_FOOD, x + 6 + i * 18, y + h - 13);
        for (int i = 0; i < mp; i++) spr_draw(c, SPR_PILE_MAT, x + w - 20 - i * 18, y + h - 13);
        break;
    }
    case CH_FUNGUS: {
        int n = ch->level + (ch->water > 30 ? 1 : 0);
        for (int i = 0; i < n + 1 && i < 4; i++) {
            spr_draw(c, SPR_MUSHROOM, x + 8 + i * 18, y + h - 15);
        }
        /* umidade */
        if (ch->water) gfx_fill(c, x + 3, y + h - 3, (w - 6) * ch->water / 100, 2, COL_WATER);
        break;
    }
    case CH_NURSERY: {
        for (int i = 0; i < g_col.eggs && i < 6; i++)
            spr_draw(c, SPR_EGG, x + 6 + i * 9, y + h - 12);
        for (int i = 0; i < g_col.larvae && i < 4; i++)
            spr_draw(c, SPR_LARVA, x + 6 + i * 14, y + h - 8);
        for (int i = 0; i < g_col.pupae && i < 3; i++)
            spr_draw(c, SPR_PUPA, x + w - 12 - i * 10, y + h - 14);
        if (g_col.eggs + g_col.larvae + g_col.pupae) {
            gfx_fill(c, x + 3, y + 2, (w - 6) * g_col.egg_progress / 100, 2, COL_GOLD);
        }
        break;
    }
    case CH_ROYAL:
        spr_draw(c, SPR_THRONE, ch->cx - 11, y + h - 18);
        break;
    case CH_TRASH:
        for (int i = 0; i < ch->dirty / 12 && i < 8; i++) {
            gfx_pixel(c, x + 6 + (i * 13) % (w - 10), y + h - 6 - (i * 7) % 8, COL_SOIL_LIGHT);
        }
        break;
    case CH_ATRIUM:
        if (ch->water) {   /* progresso de obra em andamento */
            gfx_fill(c, x, SY(ch->y) - 5, w * ch->water / 100, 3, COL_GOLD);
            gfx_rect(c, x, SY(ch->y) - 5, w, 3, COL_UI_BORDER);
        }
        break;
    default:
        break;
    }
}

void render_interior(gfx_canvas_t *c)
{
    /* céu + superfície */
    render_sky(c, VIEW_Y, 22, true);
    gfx_fill(c, 0, SY(22), VIEW_W, 2, COL_GRASS);
    gfx_fill(c, 0, SY(24), VIEW_W, 1, COL_GRASS_DK);
    gfx_fill(c, 0, SY(25), VIEW_W, VIEW_H - 25, COL_SOIL);
    soil_texture(c, 0, SY(25), VIEW_W, VIEW_H - 25);
    for (int x = 0; x < VIEW_W; x += 18) {
        if (x > 118 && x < 200) continue;      /* área do monte */
        spr_draw(c, SPR_GRASS, x, SY(16));
    }
    spr_draw(c, SPR_MOUND, 124, VIEW_Y - 2);

    /* túneis: borda clara + miolo escuro */
    for (size_t i = 0; i < sizeof(CORR) / sizeof(CORR[0]); i++) {
        int a = CORR[i].a, b = CORR[i].b;
        gfx_thick_line(c, g_node_x[a], SY(g_node_y[a]), g_node_x[b], SY(g_node_y[b]),
                       CORR[i].t + 4, COL_SOIL_LIGHT);
    }
    for (size_t i = 0; i < sizeof(CORR) / sizeof(CORR[0]); i++) {
        int a = CORR[i].a, b = CORR[i].b;
        gfx_thick_line(c, g_node_x[a], SY(g_node_y[a]), g_node_x[b], SY(g_node_y[b]),
                       CORR[i].t, COL_TUNNEL);
    }

    /* câmaras */
    for (int i = 0; i < g_col.n_chambers; i++) {
        const chamber_t *ch = &g_col.chambers[i];
        gfx_fill_round_rect(c, ch->x - 2, SY(ch->y) - 2, ch->w + 4, ch->h + 4, 6, COL_SOIL_LIGHT);
        gfx_fill_round_rect(c, ch->x, SY(ch->y), ch->w, ch->h, 5, COL_TUNNEL);
        gfx_fill(c, ch->x + 2, SY(ch->y) + ch->h - 4, ch->w - 4, 3, COL_TUNNEL_HI);
        draw_chamber_content(c, ch);
        /* rótulo */
        char lbl[14];
        snprintf(lbl, sizeof(lbl), "%s", colony_chamber_name(i));
        gtext_fit(c, ch->x + 3, SY(ch->y) + 2, ch->w - 6, lbl, COL_UI_MUTED, 1);
        if (g_col.sel_chamber == (uint8_t)(i + 1)) {
            uint16_t col = ((g_col.total_ticks / 8) & 1) ? COL_GOLD : COL_UI_ACCENT;
            gfx_round_rect(c, ch->x - 3, SY(ch->y) - 3, ch->w + 6, ch->h + 6, 6, col);
        }
    }

    /* operárias (dentro) */
    for (int i = 0; i < g_col.n_workers; i++) {
        const worker_t *w = &g_col.workers[i];
        if (!w->alive || worker_outside(w)) continue;
        draw_worker(c, w, i);
    }

    /* rainha */
    draw_queen(c);

    /* chuva molha a superfície */
    if (colony_raining()) {
        gfx_tint(c, 0, VIEW_Y, VIEW_W, 26, 25, COL_WATER);
    }
    /* noite */
    int nl = render_night_level();
    if (nl) gfx_tint(c, 0, VIEW_Y, VIEW_W, VIEW_H, nl, COL_SKY_NIGHT);
    if (g_col.weather == WX_STORM) gfx_tint(c, 0, VIEW_Y, VIEW_W, VIEW_H, 18, COL_UI_PANEL);
}

/* ============================ exterior ==================================== */
static void draw_resource(gfx_canvas_t *c, const resource_t *r)
{
    int x = r->x - g_col.cam_x, y = SY(r->y);
    if (x < -30 || x > VIEW_W + 30) return;
    if (!r->amount) {
        /* restou nada: toquinho */
        gfx_pixel(c, x, y + 2, COL_SOIL_LIGHT);
        gfx_pixel(c, x + 2, y + 3, COL_SOIL_LIGHT);
        return;
    }
    switch (r->kind) {
    case RES_LEAF:
        spr_draw(c, SPR_LEAF_PILE, x - 9, y - 8);
        if (r->amount > 2) spr_draw(c, SPR_LEAF, x - 4, y - 14);
        break;
    case RES_BERRY:
        spr_draw(c, SPR_BUSH, x - 12, y - 16);
        break;
    case RES_FLOWER:
        spr_draw(c, SPR_FLOWER_R, x - 6, y - 14);
        if (r->amount > 1) spr_draw(c, SPR_FLOWER_Y, x + 3, y - 12);
        break;
    case RES_SEED:
        spr_draw(c, SPR_SEEDS, x - 7, y - 6);
        break;
    case RES_TWIG:
        spr_draw(c, SPR_TWIG, x - 8, y - 6);
        break;
    case RES_MUSHROOM:
        spr_draw(c, SPR_MUSHROOM, x - 6, y - 9);
        if (r->amount > 1) spr_draw(c, SPR_MUSHROOM, x + 4, y - 8);
        break;
    case RES_STONE:
        spr_draw(c, SPR_PEBBLE, x - 3, y - 3);
        spr_draw(c, SPR_PEBBLE, x + 3, y - 2);
        if (r->amount > 2) spr_draw(c, SPR_PEBBLE, x, y - 6);
        break;
    case RES_PREY:
        spr_draw(c, SPR_BEETLE, x - 8, y - 9);
        break;
    default:
        break;
    }
    if (r->claimed) {
        /* alvo reservado: marcador pulsando */
        if ((g_col.total_ticks / 6) & 1) {
            gfx_pixel(c, x, y - 20, COL_GOLD);
            gfx_pixel(c, x - 1, y - 19, COL_GOLD);
            gfx_pixel(c, x + 1, y - 19, COL_GOLD);
        }
    }
}

void render_outside(gfx_canvas_t *c)
{
    int cam = g_col.cam_x;

    render_sky(c, VIEW_Y, 76, false);

    /* colinas com parallax */
    for (int wx = -80; wx < WORLD_W + 80; wx += 90) {
        int sx = wx - cam / 2;
        gfx_fill_circle(c, sx, SY(102), 46, COL_GRASS_DK);
        gfx_fill_circle(c, sx + 30, SY(104), 34, GFX_RGB(60, 116, 48));
    }

    /* chão */
    gfx_fill(c, 0, SY(100), VIEW_W, 8, COL_GRASS);
    gfx_fill(c, 0, SY(100), VIEW_W, 2, GFX_RGB(140, 210, 96));
    gfx_fill(c, 0, SY(108), VIEW_W, VIEW_H - 108, COL_SOIL);
    soil_texture(c, 0, SY(108), VIEW_W, VIEW_H - 108);
    for (int wx = (cam / 20) * 20; wx < cam + VIEW_W + 20; wx += 20) {
        spr_draw(c, SPR_GRASS, wx - cam, SY(94));
    }

    /* monte (entrada da colônia) */
    spr_draw(c, SPR_MOUND, 0 - cam, SY(88));
    if (cam > 4) {
        /* seta indicando a colônia fora da tela */
        gfx_fill_tri(c, 6, SY(60), 6, SY(72), 16, SY(66), COL_GOLD);
    }

    /* recursos */
    for (int i = 0; i < g_col.n_res; i++) draw_resource(c, &g_col.res[i]);

    /* aranha + fio */
    {
        if (g_col.spider_x >= 0) {
            int sx = g_col.spider_x - cam;
            if (sx > -20 && sx < VIEW_W + 20) {
                gfx_vline(c, sx + 9, VIEW_Y, SY(g_col.spider_y) - VIEW_Y - 4, COL_UI_MUTED);
                spr_draw(c, SPR_SPIDER, sx - 9, SY(g_col.spider_y) - 6);
                if ((g_col.total_ticks / 10) & 1) {
                    gfx_pixel(c, sx + 4, SY(g_col.spider_y) - 2, COL_DANGER);
                    gfx_pixel(c, sx + 14, SY(g_col.spider_y) - 2, COL_DANGER);
        }
            }
        }
    }

    /* operárias no exterior */
    for (int i = 0; i < g_col.n_workers; i++) {
        const worker_t *w = &g_col.workers[i];
        if (!w->alive || !worker_outside(w)) continue;
        int x = UNQ8(w->x) - cam, y = SY(UNQ8(w->y));
        sprite_id_t id;
        bool flip = w->face >= 0;   /* arte base olha p/ esquerda */
        if (w->carry) id = SPR_ANT_CARRY_R;
        else if (w->state == W_HUNT) id = ((g_col.total_ticks / 4) & 1) ? SPR_ANT_WALK_R : SPR_ANT_WALK2_R;
        else id = ((g_col.total_ticks / 6 + (uint32_t)i) & 1) ? SPR_ANT_WALK_R : SPR_ANT_WALK2_R;
        int sw = spr_w(id), sh = spr_h(id);
        if (flip) spr_draw_flip(c, id, x - sw / 2, y - sh + 3);
        else spr_draw(c, id, x - sw / 2, y - sh + 3);
        if (g_col.sel_worker == (uint8_t)(i + 1)) {
            gfx_rect(c, x - 10, y - sh - 2, 21, sh + 6, COL_GOLD);
        }
    }

    /* marcador de exploração */
    if (g_col.marker_on) {
        int mx = g_col.marker_x - cam, my = SY(g_col.marker_y);
        uint16_t col = ((g_col.total_ticks / 6) & 1) ? COL_GOLD : COL_UI_ACCENT;
        gfx_line(c, mx - 6, my, mx - 2, my, col);
        gfx_line(c, mx + 2, my, mx + 6, my, col);
        gfx_line(c, mx, my - 6, mx, my - 2, col);
        gfx_line(c, mx, my + 2, mx, my + 6, col);
    }

    /* indicação de atividade fora do campo de visão */
    for (int i = 0; i < g_col.n_workers; i++) {
        const worker_t *w = &g_col.workers[i];
        if (!w->alive || !worker_outside(w)) continue;
        int x = UNQ8(w->x) - cam;
        if (x < 0)  gfx_fill_tri(c, 2, SY(50), 2, SY(60), 10, SY(55), COL_GOLD);
        if (x >= VIEW_W) gfx_fill_tri(c, VIEW_W - 2, SY(50), VIEW_W - 2, SY(60),
                                      VIEW_W - 10, SY(55), COL_GOLD);
    }

    if (colony_raining()) gfx_tint(c, 0, VIEW_Y, VIEW_W, VIEW_H, 22, COL_WATER);
    int nl = render_night_level();
    if (nl) gfx_tint(c, 0, VIEW_Y, VIEW_W, VIEW_H, nl, COL_SKY_NIGHT);
    if (g_col.weather == WX_STORM) gfx_tint(c, 0, VIEW_Y, VIEW_W, VIEW_H, 18, COL_UI_PANEL);
}
