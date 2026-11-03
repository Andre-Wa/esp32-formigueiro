/*
 * settings_screen.c — Configurações do habitat:
 *   velocidade, brilho, WiFi (portal), cidade (clima), clima on/off,
 *   ajuste de hora, calibração de touch, orientação, salvar, novo jogo
 *   e a última linha do diário da colônia.
 *
 * A edição de cidade abre um tecladinho alfanumérico na metade de baixo.
 */
#include <string.h>
#include <stdio.h>
#include "screens_common.h"
#include "app_config.h"
#include "app_types.h"
#include "colony.h"
#include "sprites.h"
#include "nvs_config.h"
#include "net/wifi_prov.h"
#include "lcd_spi_driver.h"

#define ROW_Y0   30
#define ROW_H    19
#define ROW_W    320

enum {
    ROW_SPEED = 0,
    ROW_BRIGHT,
    ROW_WIFI,
    ROW_CITY,
    ROW_WEATHER,
    ROW_CLOCK,
    ROW_CALIB,
    ROW_ORIENT,
    ROW_SAVE,
    ROW_RESET,
    ROW_COUNT,
};

static bool    s_edit_city;
static char    s_city_buf[20];
static uint8_t s_reset_arm;      /* 1 = aguardando confirmação */
static int64_t s_reset_arm_ms;

static const char *KB_ROWS[] = {
    "QWERTYUIOP",
    "ASDFGHJKL",
    "ZXCVBNM-",
    "0123456789",
};
#define KB_X0   8
#define KB_Y0   130
#define KB_CW   30
#define KB_CH   20

/* ---------------- desenho ---------------- */
static void draw_row(gfx_canvas_t *c, int row, const char *label, const char *value,
                     uint16_t vcol)
{
    int y = ROW_Y0 + row * ROW_H;
    if ((y / 2) & 1) gfx_fill(c, 0, y, ROW_W, ROW_H, COL_UI_PANEL);
    gfx_hline(c, 0, y + ROW_H - 1, ROW_W, COL_UI_PANEL2);
    gtext(c, 8, y + 2, label, COL_UI_MUTED, 1);
    gtext_fit(c, 120, y + 2, 190, value, vcol, 1);
}

static void settings_draw(gfx_canvas_t *c)
{
    gfx_fill(c, 0, 0, LCD_H_RES, LCD_V_RES, COL_UI_BG);
    ui_draw_header(c, "Configuracoes");
    ui_draw_back_button(c);

    char v[64];
    const app_config_t *cfg = appcfg_get();

    snprintf(v, sizeof(v), "%s", g_col.speed == 0 ? "PAUSADO" :
             g_col.speed == 1 ? "normal (x1)" :
             g_col.speed == 2 ? "rapida (x2)" : "turbo (x4)");
    draw_row(c, ROW_SPEED, "VELOCIDADE", v, COL_UI_TEXT);

    snprintf(v, sizeof(v), "%u%%  [-]  [+]", cfg->brightness);
    draw_row(c, ROW_BRIGHT, "BRILHO", v, COL_UI_TEXT);

    switch (net_state()) {
    case WIFI_ONLINE:
        snprintf(v, sizeof(v), "online (%s)", cfg->wifi_ssid);
        draw_row(c, ROW_WIFI, "WIFI", v, COL_UI_OK);
        break;
    case WIFI_AP_PORTAL:
        snprintf(v, sizeof(v), "portal: %s", wifi_ap_ssid());
        draw_row(c, ROW_WIFI, "WIFI", v, COL_UI_WARN);
        break;
    case WIFI_CONNECTING:
    case WIFI_STARTING:
        draw_row(c, ROW_WIFI, "WIFI", "conectando...", COL_UI_WARN);
        break;
    default:
        draw_row(c, ROW_WIFI, "WIFI", "toque p/ configurar", COL_UI_BAD);
        break;
    }

    snprintf(v, sizeof(v), "%s (%.2f, %.2f)", cfg->city,
             cfg->lat_x100 / 100.0, cfg->lon_x100 / 100.0);
    draw_row(c, ROW_CITY, "CIDADE", v, COL_UI_TEXT);

    draw_row(c, ROW_WEATHER, "CLIMA REAL", cfg->weather_enabled ? "ligado" : "desligado",
             cfg->weather_enabled ? COL_UI_OK : COL_UI_MUTED);

    snprintf(v, sizeof(v), "%02d:%02d  [+1h]", colony_hour(), colony_minute());
    draw_row(c, ROW_CLOCK, "HORA", v, g_col.time_sync ? COL_UI_OK : COL_UI_WARN);

    draw_row(c, ROW_CALIB, "TOUCH", "calibrar novamente", COL_UI_TEXT);
    draw_row(c, ROW_ORIENT, "TELA", "girar/espelhar", COL_UI_TEXT);
    draw_row(c, ROW_SAVE, "SAVE", "gravar agora", COL_UI_TEXT);
    draw_row(c, ROW_RESET, "COLENIA", s_reset_arm ? "TOQUE DE NOVO P/ APAGAR!" : "novo jogo (apaga save)",
             s_reset_arm ? COL_UI_BAD : COL_UI_TEXT);

    /* diário */
    int y = ROW_Y0 + ROW_COUNT * ROW_H + 2;
    gfx_hline(c, 0, y - 2, LCD_H_RES, COL_UI_BORDER);
    if (g_col.event_count) {
        int idx = (g_col.event_head + COLONY_MAX_EVENTS - 1) % COLONY_MAX_EVENTS;
        gtext_fit(c, 6, y, LCD_H_RES - 12, g_col.events[idx], COL_UI_MUTED, 1);
        idx = (g_col.event_head + COLONY_MAX_EVENTS - 2) % COLONY_MAX_EVENTS;
        if (g_col.event_count > 1) {
            gtext_fit(c, 6, y + 14, LCD_H_RES - 12, g_col.events[idx], COL_UI_MUTED, 1);
        }
    }
    ui_draw_toast(c);

    /* -------- teclado da cidade -------- */
    if (s_edit_city) {
        gfx_tint(c, 0, 0, LCD_H_RES, LCD_V_RES, 60, COL_UI_BG);
        gfx_fill_round_rect(c, 4, KB_Y0 - 26, 312, 132, 8, COL_UI_PANEL);
        gfx_round_rect(c, 4, KB_Y0 - 26, 312, 132, 8, COL_UI_ACCENT);
        gtext(c, 12, KB_Y0 - 22, "Cidade p/ clima:", COL_UI_ACCENT, 1);
        char buf[28];
        snprintf(buf, sizeof(buf), ">%s_", s_city_buf);
        gtext(c, 12, KB_Y0 - 8, buf, COL_UI_TEXT, 1);
        for (int r = 0; r < 4; r++) {
            int n = (int)strlen(KB_ROWS[r]);
            for (int i = 0; i < n; i++) {
                int x = KB_X0 + i * KB_CW + (r == 1 ? 15 : r == 2 ? 30 : 0);
                int y2 = KB_Y0 + 8 + r * KB_CH;
                gfx_fill_round_rect(c, x, y2, KB_CW - 3, KB_CH - 4, 4, COL_UI_PANEL2);
                char ch[2] = { KB_ROWS[r][i], 0 };
                gtext_center(c, x + (KB_CW - 3) / 2, y2 + 1, ch, COL_UI_TEXT, 1);
            }
        }
        /* botões especiais */
        gfx_fill_round_rect(c, 250, KB_Y0 + 8, 60, KB_CH - 4, 4, COL_UI_PANEL2);
        gtext_center(c, 280, KB_Y0 + 9, "APAGA", COL_UI_TEXT, 1);
        gfx_fill_round_rect(c, 250, KB_Y0 + 28, 60, KB_CH - 4, 4, COL_UI_OK);
        gtext_center(c, 280, KB_Y0 + 29, "OK", COL_UI_BG, 1);
        gfx_fill_round_rect(c, 250, KB_Y0 + 48, 60, KB_CH - 4, 4, COL_UI_PANEL2);
        gtext_center(c, 280, KB_Y0 + 49, "CANCEL", COL_UI_MUTED, 1);
    }
}

/* ---------------- toque ---------------- */
static bool kb_hit(int x, int y)
{
    if (!s_edit_city) return false;
    for (int r = 0; r < 4; r++) {
        int n = (int)strlen(KB_ROWS[r]);
        for (int i = 0; i < n; i++) {
            int kx = KB_X0 + i * KB_CW + (r == 1 ? 15 : r == 2 ? 30 : 0);
            int ky = KB_Y0 + 8 + r * KB_CH;
            if (gfx_in_rect(x, y, kx, ky, KB_CW - 3, KB_CH - 4)) {
                size_t l = strlen(s_city_buf);
                if (l < sizeof(s_city_buf) - 2) {
                    s_city_buf[l] = KB_ROWS[r][i];
                    s_city_buf[l + 1] = 0;
                }
                return true;
            }
        }
    }
    if (gfx_in_rect(x, y, 250, KB_Y0 + 8, 60, KB_CH - 4)) {        /* apaga */
        size_t l = strlen(s_city_buf);
        if (l) s_city_buf[l - 1] = 0;
        return true;
    }
    if (gfx_in_rect(x, y, 250, KB_Y0 + 28, 60, KB_CH - 4)) {       /* ok */
        if (s_city_buf[0]) {
            app_send_cmd_str(CMD_SET_CITY, s_city_buf);
        }
        s_edit_city = false;
        ui_invalidate_all();
        return true;
    }
    if (gfx_in_rect(x, y, 250, KB_Y0 + 48, 60, KB_CH - 4)) {       /* cancela */
        s_edit_city = false;
        ui_invalidate_all();
        return true;
    }
    return true;   /* qualquer toque no véu não vaza */
}

static void settings_touch(const touch_evt_t *ev)
{
    if (ev->type != TEV_TAP) return;

    if (ui_back_button_hit(ev->x, ev->y) && !s_edit_city) {
        ui_show(SCREEN_GAME);
        return;
    }
    if (s_edit_city) { kb_hit(ev->x, ev->y); ui_invalidate_all(); return; }

    int row = (ev->y - ROW_Y0) / ROW_H;
    if (ev->y < ROW_Y0 || row >= ROW_COUNT) return;

    app_config_t *cfg = appcfg_get();
    switch (row) {
    case ROW_SPEED: {
        int nx = g_col.speed == 1 ? 2 : g_col.speed == 2 ? 4 : g_col.speed == 4 ? 0 : 1;
        app_send_cmd(CMD_SET_SPEED, nx);
        break;
    }
    case ROW_BRIGHT: {
        int b = cfg->brightness;
        if (ev->x > 240) b += 15; else if (ev->x > 200) b -= 15; else b = (b + 25) % 101;
        if (b < 10) b = 10;
        if (b > 100) b = 100;
        app_send_cmd(CMD_SET_BRIGHTNESS, b);
        break;
    }
    case ROW_WIFI:
        if (net_state() == WIFI_ONLINE) {
            colony_msg("WiFi ja conectado.");
        } else {
            app_send_cmd(CMD_START_PROVISION, 0);
            colony_msg("Portal WiFi abrindo...");
        }
        break;
    case ROW_CITY:
        strlcpy(s_city_buf, cfg->city, sizeof(s_city_buf));
        s_edit_city = true;
        break;
    case ROW_WEATHER:
        cfg->weather_enabled = !cfg->weather_enabled;
        appcfg_save();
        colony_msg("Clima real %s.", cfg->weather_enabled ? "ligado" : "desligado");
        break;
    case ROW_CLOCK:
        app_send_cmd(CMD_SET_CLOCK, 1);
        break;
    case ROW_CALIB:
        ui_show(SCREEN_CALIB);
        return;
    case ROW_ORIENT:
        ui_show(SCREEN_ORIENT);
        return;
    case ROW_SAVE:
        app_send_cmd(CMD_SAVE_NOW, 0);
        colony_msg("Jogo salvo.");
        break;
    case ROW_RESET:
        if (!s_reset_arm) {
            s_reset_arm = 1;
            s_reset_arm_ms = ui_now_ms();
        } else {
            s_reset_arm = 0;
            app_send_cmd(CMD_RESET_GAME, 0);
        }
        break;
    default:
        break;
    }
    if (s_reset_arm && ui_now_ms() - s_reset_arm_ms > 4000) s_reset_arm = 0;
    ui_invalidate_all();
}

static void settings_enter(void)
{
    s_edit_city = false;
    s_reset_arm = 0;
}

const ui_screen_t settings_screen = {
    .name = "settings",
    .on_enter = settings_enter,
    .draw = settings_draw,
    .on_touch = settings_touch,
};
