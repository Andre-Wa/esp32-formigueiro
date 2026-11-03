/*
 * game_screen.c — Tela principal: HUD, cena (interior/exterior), toque e
 * menu de ações. É aqui que o habitat "acontece" para o usuário.
 *
 * Layout (320x240):
 *   y   0..23   barra de status (dia/hora, clima, estoques, WiFi, engrenagem)
 *   y  24..201  cena (interior OU exterior; arraste horizontal troca)
 *   y 186..201  ticker de mensagens (sobreposto à cena)
 *   y 202..239  barra de ação: abas + atalhos + menu
 */
#include <string.h>
#include <stdio.h>
#include "screens_common.h"
#include "ui_engine.h"
#include "app_config.h"
#include "app_types.h"
#include "colony.h"
#include "render.h"
#include "sprites.h"
#include "weather.h"
#include "nvs_config.h"

/* ---------------- geometria da barra inferior ---------------- */
#define BAR_Y        202
#define BTN_H        38
#define TAB_HOME_X   2
#define TAB_OUT_X    31
#define TAB_W        28
#define Q0_X         60      /* alimentar  */
#define Q1_X         97      /* brincar    */
#define Q2_X         134     /* caçar      */
#define Q3_X         171     /* descansar  */
#define Q4_X         208     /* trabalhar  */
#define MENU_X       245
#define EGG_X        278
#define QBTN_W       36
#define MENU_W       32
#define EGG_W        40

/* ---------------- menu grade ---------------- */
typedef struct { cmd_id_t cmd; const char *label; sprite_id_t ico; } menu_item_t;
static const menu_item_t MENU_ITEMS[] = {
    { CMD_PET,     "CARINHO",  SPR_ICO_PET   },
    { CMD_FEED,    "ALIMENTAR",SPR_ICO_FEED  },
    { CMD_PLAY,    "BRINCAR",  SPR_ICO_PLAY  },
    { CMD_GROOM,   "BANHO",    SPR_ICO_CLEAN },
    { CMD_REST,    "DORMIR",   SPR_ICO_REST  },
    { CMD_HUNT,    "CACAR",    SPR_ICO_HUNT  },
    { CMD_BUILD,   "AMPLIAR",  SPR_ICO_WORK  },
    { CMD_TEND,    "CUIDAR",   SPR_ICO_EGG   },
    { CMD_FORAGE,  "COLETAR",  SPR_ICO_LEAF  },
    { CMD_LAY_EGG, "POR OVOS", SPR_ICO_EGG   },
};
#define MENU_COLS 3
#define MENU_ROWS 4      /* 10 itens + FECHAR na última célula */
#define MENU_X0    44
#define MENU_Y0    40
#define MENU_CW    78
#define MENU_CH    34

#define SY_RES(y) (y)   /* recursos já ficam em coords locais da vista */

static uint8_t s_view;          /* 0 = interior, 1 = exterior */
static bool    s_menu_open;
static bool    s_drag_consumed;

/* ================= desenho ================= */
static void draw_status_bar(gfx_canvas_t *c)
{
    gfx_fill(c, 0, 0, LCD_H_RES, 24, COL_UI_PANEL);
    gfx_hline(c, 0, 23, LCD_H_RES, COL_UI_BORDER);

    char t[16];
    snprintf(t, sizeof(t), "D%lu", (unsigned long)g_col.day);
    gtext(c, 4, 4, t, COL_UI_ACCENT, 1);
    snprintf(t, sizeof(t), "%02d:%02d", colony_hour(), colony_minute());
    gtext(c, 34, 4, t, COL_UI_TEXT, 1);

    /* clima */
    sprite_id_t wi = SPR_SUN;
    switch (g_col.weather) {
    case WX_SUN:        wi = SPR_SUN; break;
    case WX_PARTLY:     wi = SPR_CLOUD; break;
    case WX_CLOUD:      wi = SPR_CLOUD; break;
    case WX_FOG:        wi = SPR_ICO_FOG; break;
    case WX_RAIN:       wi = SPR_ICO_RAIN; break;
    case WX_STORM:      wi = SPR_ICO_STORM; break;
    case WX_SNOW:       wi = SPR_ICO_SNOW; break;
    default:            wi = SPR_SUN; break;
    }
    spr_draw(c, wi, 78, 4);
    snprintf(t, sizeof(t), "%d", (g_col.temp_x10 + 5) / 10);
    gtext(c, 96, 4, t, COL_UI_TEXT, 1);
    gtext(c, 112, 4, "C", COL_UI_MUTED, 1);

    /* estoques */
    spr_draw(c, SPR_ICO_LEAF, 126, 6);
    snprintf(t, sizeof(t), "%d", g_col.food);
    gtext(c, 138, 4, t, g_col.food < 10 ? COL_UI_BAD : COL_UI_OK, 1);

    spr_draw(c, SPR_ICO_MAT, 166, 6);
    snprintf(t, sizeof(t), "%d", g_col.material);
    gtext(c, 178, 4, t, COL_UI_TEXT, 1);

    spr_draw(c, SPR_ICO_EGG, 204, 6);
    snprintf(t, sizeof(t), "%d", g_col.eggs + g_col.larvae + g_col.pupae);
    gtext(c, 216, 4, t, COL_UI_TEXT, 1);

    /* velocidade */
    if (g_col.speed == 0) gtext(c, 238, 4, "||", COL_UI_WARN, 1);
    else {
        snprintf(t, sizeof(t), "x%d", g_col.speed);
        gtext(c, 238, 4, t, g_col.speed > 1 ? COL_UI_WARN : COL_UI_MUTED, 1);
    }

    /* wifi */
    spr_draw(c, SPR_ICO_WIFI, 258, 6);
    if (!g_col.net_ok) {
        gfx_line(c, 258, 6, 268, 16, COL_UI_BAD);
    }

    /* alarme */
    if (g_col.alarm && ((g_col.total_ticks / 8) & 1)) {
        gfx_fill_circle(c, 276, 12, 4, g_col.alarm == 1 ? COL_UI_WARN :
                        g_col.alarm == 2 ? COL_UI_BAD : COL_UI_INFO);
        gtext(c, 274, 8, "!", COL_UI_BG, 1);
    }

    /* engrenagem (configurações) */
    gfx_fill_round_rect(c, 292, 2, 22, 20, 4, COL_UI_PANEL2);
    gfx_round_rect(c, 292, 2, 22, 20, 4, COL_UI_BORDER);
    spr_draw_centered(c, SPR_ICO_GEAR, 303, 12);
}

static void draw_bottom_bar(gfx_canvas_t *c)
{
    gfx_fill(c, 0, BAR_Y, LCD_H_RES, LCD_V_RES - BAR_Y, COL_UI_PANEL);
    gfx_hline(c, 0, BAR_Y, LCD_H_RES, COL_UI_BORDER);

    /* abas de cena */
    gfx_fill_round_rect(c, TAB_HOME_X, BAR_Y + 2, TAB_W, BTN_H - 4, 4,
                        s_view == 0 ? COL_UI_ACCENT : COL_UI_PANEL2);
    spr_draw_centered(c, SPR_ICO_HOME, TAB_HOME_X + TAB_W / 2, BAR_Y + 13);
    gtext_center(c, TAB_HOME_X + TAB_W / 2, BAR_Y + 22, "NINHO",
                 s_view == 0 ? COL_UI_BG : COL_UI_MUTED, 1);

    gfx_fill_round_rect(c, TAB_OUT_X, BAR_Y + 2, TAB_W, BTN_H - 4, 4,
                        s_view == 1 ? COL_UI_ACCENT : COL_UI_PANEL2);
    spr_draw_centered(c, SPR_ICO_TREE, TAB_OUT_X + TAB_W / 2, BAR_Y + 13);
    gtext_center(c, TAB_OUT_X + TAB_W / 2, BAR_Y + 22, "FORA",
                 s_view == 1 ? COL_UI_BG : COL_UI_MUTED, 1);

    /* atalhos */
    struct { int x; sprite_id_t ico; const char *lbl; } q[] = {
        { Q0_X, SPR_ICO_FEED, "DAR"  },
        { Q1_X, SPR_ICO_PLAY, "BRIN" },
        { Q2_X, SPR_ICO_HUNT, "CACA" },
        { Q3_X, SPR_ICO_REST, "DORM" },
        { Q4_X, SPR_ICO_WORK, "OBRA" },
    };
    for (size_t i = 0; i < sizeof(q) / sizeof(q[0]); i++) {
        gfx_fill_round_rect(c, q[i].x, BAR_Y + 2, QBTN_W, BTN_H - 4, 4, COL_UI_PANEL2);
        gfx_round_rect(c, q[i].x, BAR_Y + 2, QBTN_W, BTN_H - 4, 4, COL_UI_BORDER);
        spr_draw_centered(c, q[i].ico, q[i].x + QBTN_W / 2, BAR_Y + 13);
        gtext_center(c, q[i].x + QBTN_W / 2, BAR_Y + 22, q[i].lbl, COL_UI_TEXT, 1);
    }

    /* menu */
    gfx_fill_round_rect(c, MENU_X, BAR_Y + 2, MENU_W, BTN_H - 4, 4,
                        s_menu_open ? COL_UI_ACCENT : COL_UI_PANEL2);
    for (int i = 0; i < 3; i++) {
        gfx_fill(c, MENU_X + 8, BAR_Y + 10 + i * 6, 16, 2,
                 s_menu_open ? COL_UI_BG : COL_UI_TEXT);
    }
    gtext_center(c, MENU_X + MENU_W / 2, BAR_Y + 22, "MAIS", COL_UI_MUTED, 1);

    /* ovos */
    gfx_fill_round_rect(c, EGG_X, BAR_Y + 2, EGG_W, BTN_H - 4, 4, COL_UI_PANEL2);
    gfx_round_rect(c, EGG_X, BAR_Y + 2, EGG_W, BTN_H - 4, 4, COL_UI_BORDER);
    spr_draw_centered(c, SPR_ICO_EGG, EGG_X + EGG_W / 2, BAR_Y + 13);
    gtext_center(c, EGG_X + EGG_W / 2, BAR_Y + 22, "OVOS", COL_UI_TEXT, 1);
}

static void draw_ticker(gfx_canvas_t *c)
{
    if (!g_col.msg_ticks || !g_col.msg[0]) return;
    int y = VIEW_Y + VIEW_H - 18;
    gfx_fill(c, 0, y, LCD_H_RES, 18, COL_UI_BG);
    gfx_hline(c, 0, y, LCD_H_RES, COL_UI_BORDER);
    gtext_fit(c, 6, y + 1, LCD_H_RES - 12, g_col.msg, COL_UI_ACCENT, 1);
}

static void draw_particles(gfx_canvas_t *c)
{
    for (int i = 0; i < g_col.n_parts; i++) {
        const particle_t *p = &g_col.parts[i];
        int a = p->life * 255 / (p->max_life ? p->max_life : 1);
        switch (p->kind) {
        case P_HEART:
            spr_draw_centered(c, SPR_HEART, p->x, p->y);
            break;
        case P_ZZZ:
            gtext(c, p->x, p->y, "z", COL_UI_MUTED, 1);
            break;
        case P_SPARK:
            spr_draw_centered(c, SPR_SPARK, p->x, p->y);
            break;
        case P_DIRT:
            gfx_pixel(c, p->x, p->y, p->col);
            gfx_pixel(c, p->x + 1, p->y + 1, p->col);
            break;
        case P_DROP:
            gfx_line(c, p->x, p->y, p->x - 1, p->y + 4, p->col);
            break;
        case P_TEXT:
            gtext_bg(c, p->x, p->y, p->txt, p->col, COL_UI_BG, 1);
            break;
        default:
            break;
        }
        (void)a;
    }
}

static void draw_menu(gfx_canvas_t *c)
{
    if (!s_menu_open) return;
    /* véu escuro sobre a cena; o painel do menu vem por cima */
    gfx_tint(c, 0, 0, LCD_H_RES, LCD_V_RES, 62, COL_UI_BG);

    gfx_fill_round_rect(c, MENU_X0 - 6, MENU_Y0 - 22, MENU_COLS * MENU_CW + 12,
                        MENU_ROWS * MENU_CH + 30, 8, COL_UI_PANEL);
    gfx_round_rect(c, MENU_X0 - 6, MENU_Y0 - 22, MENU_COLS * MENU_CW + 12,
                   MENU_ROWS * MENU_CH + 30, 8, COL_UI_ACCENT);
    gtext_center(c, LCD_H_RES / 2, MENU_Y0 - 18, "Cuidar da colonia", COL_UI_ACCENT, 1);

    for (int i = 0; i < MENU_COLS * MENU_ROWS; i++) {
        int cx = MENU_X0 + (i % MENU_COLS) * MENU_CW;
        int cy = MENU_Y0 + (i / MENU_COLS) * MENU_CH;
        if (i < (int)(sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]))) {
            gfx_fill_round_rect(c, cx + 2, cy + 2, MENU_CW - 6, MENU_CH - 6, 5, COL_UI_PANEL2);
            gfx_round_rect(c, cx + 2, cy + 2, MENU_CW - 6, MENU_CH - 6, 5, COL_UI_BORDER);
            spr_draw(c, MENU_ITEMS[i].ico, cx + 6, cy + 8);
            gtext(c, cx + 22, cy + 10, MENU_ITEMS[i].label, COL_UI_TEXT, 1);
        } else if (i == MENU_COLS * MENU_ROWS - 1) {
            gfx_fill_round_rect(c, cx + 2, cy + 2, MENU_CW - 6, MENU_CH - 6, 5, COL_UI_PANEL2);
            gfx_round_rect(c, cx + 2, cy + 2, MENU_CW - 6, MENU_CH - 6, 5, COL_UI_BORDER);
            spr_draw(c, SPR_ICO_BACK, cx + 6, cy + 10);
            gtext(c, cx + 22, cy + 10, "FECHAR", COL_UI_MUTED, 1);
        }
    }
}

static void game_draw(gfx_canvas_t *c)
{
    if (s_view == 0) render_interior(c);
    else render_outside(c);
    draw_particles(c);
    draw_ticker(c);
    draw_status_bar(c);
    draw_bottom_bar(c);
    draw_menu(c);
}

/* ================= marcas de sujeira (render parcial) ================= */
static int s_last_min = -1, s_last_food = -1, s_last_mat = -1, s_last_brood = -1,
           s_last_speed = -1, s_last_net = -1, s_last_wx = -1, s_last_msg = -1,
           s_last_sel = -1, s_last_menu = -1, s_last_alarm = -1;

void game_screen_refresh(void)
{
    /* a cena anima sempre */
    ui_dirty_rect(0, VIEW_Y, VIEW_W, VIEW_H);

    int min = colony_hour() * 60 + colony_minute();
    if (min != s_last_min || g_col.food != s_last_food ||
        g_col.material != s_last_mat ||
        g_col.eggs + g_col.larvae + g_col.pupae != s_last_brood ||
        g_col.speed != s_last_speed || (int)g_col.net_ok != s_last_net ||
        g_col.weather != s_last_wx || (g_col.alarm != s_last_alarm)) {
        s_last_min = min; s_last_food = g_col.food; s_last_mat = g_col.material;
        s_last_brood = g_col.eggs + g_col.larvae + g_col.pupae;
        s_last_speed = g_col.speed; s_last_net = (int)g_col.net_ok;
        s_last_wx = g_col.weather; s_last_alarm = g_col.alarm;
        ui_dirty_rect(0, 0, LCD_H_RES, 24);
    }
    int msgv = (g_col.msg_ticks ? 1 : 0) * 1000 + (g_col.msg[0] & 0xFF) +
               (int)g_col.msg_ticks / 16;
    int selv = g_col.sel_chamber * 64 + g_col.sel_worker;
    if (msgv != s_last_msg || selv != s_last_sel ||
        (int)s_menu_open != s_last_menu) {
        s_last_msg = msgv; s_last_sel = selv; s_last_menu = (int)s_menu_open;
        ui_dirty_rect(0, VIEW_Y + VIEW_H - 20, LCD_H_RES, LCD_V_RES - VIEW_Y - VIEW_H + 20 + 38);
    }
}

/* ================= toque ================= */
static void switch_view(uint8_t v)
{
    if (v == s_view) return;
    s_view = v;
    ui_dirty_rect(0, VIEW_Y, VIEW_W, VIEW_H + 40);
}

static void handle_view_tap(int x, int y)
{
    int ly = y - VIEW_Y;
    if (s_view == 0) {
        if (colony_queen_at(x, ly)) { app_send_cmd(CMD_PET, 0); return; }
        int wi = colony_worker_at(x, ly);
        if (wi >= 0) { app_send_cmd(CMD_TAP_WORKER, wi); return; }
        int ci = colony_chamber_at(x, ly);
        if (ci >= 0) { app_send_cmd(CMD_TAP_CHAMBER, ci); return; }
        /* toque vazio: limpa seleção */
        g_col.sel_chamber = 0;
        g_col.sel_worker = 0;
    } else {
        /* recursos: raio generoso p/ dedo */
        for (int i = 0; i < g_col.n_res; i++) {
            const resource_t *r = &g_col.res[i];
            if (!r->amount) continue;
            int sx = r->x - g_col.cam_x;
            int dx = sx - x, dy = (SY_RES(r->y)) - ly;
            if (dx * dx + dy * dy < 18 * 18) {
                app_send_cmd(CMD_TAP_RESOURCE, i);
                return;
            }
        }
        if (ly >= 84 && ly <= 130) {
            app_cmd_t c = { .id = CMD_TAP_GROUND };
            c.val = x + g_col.cam_x;
            c.val2 = ly;
            if (g_cmd_queue) xQueueSend(g_cmd_queue, &c, 0);
            return;
        }
    }
}

static void handle_menu_tap(int x, int y)
{
    for (int i = 0; i < MENU_COLS * MENU_ROWS; i++) {
        int cx = MENU_X0 + (i % MENU_COLS) * MENU_CW;
        int cy = MENU_Y0 + (i / MENU_COLS) * MENU_CH;
        if (gfx_in_rect(x, y, cx + 2, cy + 2, MENU_CW - 6, MENU_CH - 6)) {
            if (i < (int)(sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]))) {
                app_send_cmd(MENU_ITEMS[i].cmd, 0);
            }
            s_menu_open = false;
            ui_dirty_all();
            return;
        }
    }
    s_menu_open = false;      /* tocou fora: fecha */
    ui_dirty_all();
}

static void game_touch(const touch_evt_t *ev)
{
    if (ev->type == TEV_PRESS) {
        s_drag_consumed = false;
        return;
    }
    if (ev->type == TEV_DRAG) {
        if (!s_drag_consumed && ev->y > VIEW_Y && ev->y < BAR_Y &&
            (ev->dx >= 24 || ev->dx <= -24)) {
            s_drag_consumed = true;
            switch_view(ev->dx < 0 ? 1 : 0);
        }
        return;
    }
    if (ev->type == TEV_LONG) {
        if (ev->y > VIEW_Y && ev->y < BAR_Y) {
            s_menu_open = !s_menu_open;
            ui_dirty_all();
        }
        return;
    }
    if (ev->type != TEV_TAP) return;

    if (s_menu_open) { handle_menu_tap(ev->x, ev->y); return; }

    /* barra de status */
    if (ev->y < 24) {
        if (gfx_in_rect(ev->x, ev->y, 292, 2, 22, 20)) {
            ui_show(SCREEN_SETTINGS);
        } else if (gfx_in_rect(ev->x, ev->y, 236, 2, 22, 20)) {
            /* cicla velocidade: 1 -> 2 -> 4 -> pausa -> 1 */
            int nx = g_col.speed == 1 ? 2 : g_col.speed == 2 ? 4 :
                     g_col.speed == 4 ? 0 : 1;
            app_send_cmd(CMD_SET_SPEED, nx);
            ui_dirty_rect(0, 0, LCD_H_RES, 24);
        }
        return;
    }

    /* barra inferior */
    if (ev->y >= BAR_Y) {
        if (gfx_in_rect(ev->x, ev->y, TAB_HOME_X, BAR_Y, TAB_W, BTN_H)) switch_view(0);
        else if (gfx_in_rect(ev->x, ev->y, TAB_OUT_X, BAR_Y, TAB_W, BTN_H)) switch_view(1);
        else if (gfx_in_rect(ev->x, ev->y, Q0_X, BAR_Y, QBTN_W, BTN_H)) app_send_cmd(CMD_FEED, 0);
        else if (gfx_in_rect(ev->x, ev->y, Q1_X, BAR_Y, QBTN_W, BTN_H)) app_send_cmd(CMD_PLAY, 0);
        else if (gfx_in_rect(ev->x, ev->y, Q2_X, BAR_Y, QBTN_W, BTN_H)) app_send_cmd(CMD_HUNT, 0);
        else if (gfx_in_rect(ev->x, ev->y, Q3_X, BAR_Y, QBTN_W, BTN_H)) app_send_cmd(CMD_REST, 0);
        else if (gfx_in_rect(ev->x, ev->y, Q4_X, BAR_Y, QBTN_W, BTN_H)) app_send_cmd(CMD_BUILD, 0);
        else if (gfx_in_rect(ev->x, ev->y, MENU_X, BAR_Y, MENU_W, BTN_H)) {
            s_menu_open = true;
            ui_dirty_all();
        } else if (gfx_in_rect(ev->x, ev->y, EGG_X, BAR_Y, EGG_W, BTN_H)) {
            app_send_cmd(CMD_LAY_EGG, 0);
        }
        return;
    }

    /* cena */
    if (ev->y >= VIEW_Y && ev->y < BAR_Y) handle_view_tap(ev->x, ev->y);
}

static void game_enter(void)
{
    s_menu_open = false;
}

const ui_screen_t game_screen = {
    .name = "game",
    .on_enter = game_enter,
    .draw = game_draw,
    .on_touch = game_touch,
};
