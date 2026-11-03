/*
 * ui_engine.c — engine de render por tiras + despacho de touch.
 *
 * Diferente do cyd-player, aqui a SIMULAÇÃO e o RENDER vivem na mesma tarefa
 * (game_task, em main.c): ela chama ui_pump() a cada tick, que consome filas,
 * despacha touch e renderiza apenas as tiras sujas.
 */
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "app_config.h"
#include "app_types.h"
#include "ui_engine.h"
#include "lcd_spi_driver.h"
#include "touch_xpt2046.h"
#include "gfx_ext.h"

extern const ui_screen_t game_screen;
extern const ui_screen_t settings_screen;
extern const ui_screen_t calib_screen;
extern const ui_screen_t orient_screen;

static const char *TAG = "ui";

/* ================== splash ================== */
static void splash_draw(gfx_canvas_t *c)
{
    gfx_fill(c, 0, 0, LCD_H_RES, LCD_V_RES, COL_UI_BG);
    gtext_center(c, LCD_H_RES / 2, 70, "FORMIGUEIRO", COL_UI_ACCENT, 2);
    gtext_center(c, LCD_H_RES / 2, 110, "habitat virtual v" APP_VERSION, COL_UI_MUTED, 1);
    gtext_center(c, LCD_H_RES / 2, 150, "acordando a colonia...", COL_UI_TEXT, 1);
}

static const ui_screen_t splash_screen = {
    .name = "splash",
    .draw = splash_draw,
};

static const ui_screen_t *s_screens[SCREEN_COUNT];

/* ================== estado interno ================== */
#define N_STRIPS ((LCD_V_RES + LCD_STRIP_LINES - 1) / LCD_STRIP_LINES)

static uint16_t *s_buf[2];
static volatile uint32_t s_dirty;         /* bitmask de tiras sujas */
static QueueHandle_t s_touch_q;
static QueueHandle_t s_ctrl_q;
static screen_id_t s_cur = SCREEN_SPLASH;

typedef struct { screen_id_t id; } ctrl_msg_t;

int64_t ui_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

void ui_dirty_all(void)
{
    s_dirty = (1u << N_STRIPS) - 1;
}

void ui_dirty_rect(int x, int y, int w, int h)
{
    (void)x; (void)w;
    int y0 = y / LCD_STRIP_LINES;
    int y1 = (y + h - 1) / LCD_STRIP_LINES;
    if (y0 < 0) y0 = 0;
    if (y1 >= N_STRIPS) y1 = N_STRIPS - 1;
    for (int n = y0; n <= y1; n++) s_dirty |= (1u << n);
}

void ui_invalidate_all(void) { ui_dirty_all(); }

void ui_show(screen_id_t id)
{
    if (id >= SCREEN_COUNT) return;
    ctrl_msg_t m = { .id = id };
    if (s_ctrl_q) xQueueSend(s_ctrl_q, &m, pdMS_TO_TICKS(100));
}

/* ================== render por tiras ================== */
static void render_strips(uint32_t mask)
{
    const ui_screen_t *scr = s_screens[s_cur];
    if (!scr || !scr->draw) return;
    for (int n = 0; n < N_STRIPS; n++) {
        if (!(mask & (1u << n))) continue;
        int y = n * LCD_STRIP_LINES;
        int h = LCD_V_RES - y < LCD_STRIP_LINES ? LCD_V_RES - y : LCD_STRIP_LINES;

        gfx_canvas_t cv;
        gfx_canvas_init(&cv, s_buf[n & 1], LCD_H_RES, h, 0, y);
        scr->draw(&cv);
        lcd_flush_async(0, y, LCD_H_RES, h, s_buf[n & 1]);
    }
}

/* ================== eventos de touch -> tela atual ================== */
static void pump_touch(void)
{
    touch_evt_t te;
    while (xQueueReceive(s_touch_q, &te, 0) == pdTRUE) {
        const ui_screen_t *scr = s_screens[s_cur];
        if (scr && scr->on_touch) scr->on_touch(&te);
    }
}

static void pump_ctrl(void)
{
    ctrl_msg_t m;
    while (xQueueReceive(s_ctrl_q, &m, 0) == pdTRUE) {
        if (m.id != s_cur) {
            if (s_screens[s_cur] && s_screens[s_cur]->on_exit) s_screens[s_cur]->on_exit();
            s_cur = m.id;
            if (s_screens[s_cur] && s_screens[s_cur]->on_enter) s_screens[s_cur]->on_enter();
            ESP_LOGI(TAG, "tela -> %s", s_screens[s_cur]->name);
            ui_dirty_all();
        }
    }
}

void ui_pump(void)
{
    pump_ctrl();
    pump_touch();
    uint32_t mask = s_dirty;
    if (mask) {
        s_dirty = 0;
        render_strips(mask);
    }
}

/* ================== tarefa de touch (Core 1, prio 5) ================== */
static void touch_task(void *arg)
{
    bool pressed = false, dragging = false, long_sent = false;
    int px = 0, py = 0, lx = 0, ly = 0;
    int64_t t0 = 0;
    int64_t last_release_ms = 0;

    while (true) {
        uint16_t x, y, rx, ry;
        bool down = xpt2046_read_full(&x, &y, &rx, &ry);
        touch_evt_t ev = { 0 };

        if (down && !pressed) {
            if (ui_now_ms() - last_release_ms < 60) {      /* bounce resistivo */
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            pressed = true; dragging = false; long_sent = false;
            px = lx = x; py = ly = y;
            t0 = ui_now_ms();
            ev.type = TEV_PRESS;
            ev.x = x; ev.y = y; ev.rx = rx; ev.ry = ry;
            xQueueSend(s_touch_q, &ev, 0);
        } else if (down && pressed) {
            if (!long_sent && ui_now_ms() - t0 >= 900) {
                long_sent = true;
                ev.type = TEV_LONG;
                ev.x = px; ev.y = py; ev.rx = rx; ev.ry = ry;
                xQueueSend(s_touch_q, &ev, 0);
            }
            int dx = x - lx, dy = y - ly;
            if (!dragging && (abs(x - px) > 10 || abs(y - py) > 10)) dragging = true;
            if (dragging && (abs(dx) >= 6 || abs(dy) >= 6)) {
                ev.type = TEV_DRAG;
                ev.x = x; ev.y = y; ev.dx = dx; ev.dy = dy;
                ev.rx = rx; ev.ry = ry;
                xQueueSend(s_touch_q, &ev, 0);
                lx = x; ly = y;
            }
        } else if (!down && pressed) {
            pressed = false;
            last_release_ms = ui_now_ms();
            if (!dragging && !long_sent && ui_now_ms() - t0 >= 40 &&
                ui_now_ms() - t0 < 800) {
                ev.type = TEV_TAP;
                ev.x = px; ev.y = py; ev.rx = rx; ev.ry = ry;
                xQueueSend(s_touch_q, &ev, 0);
            }
            ev.type = TEV_RELEASE;
            ev.x = px; ev.y = py;
            xQueueSend(s_touch_q, &ev, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ================== init ================== */
void ui_init(void)
{
    s_screens[SCREEN_SPLASH]   = &splash_screen;
    s_screens[SCREEN_GAME]     = &game_screen;
    s_screens[SCREEN_SETTINGS] = &settings_screen;
    s_screens[SCREEN_CALIB]    = &calib_screen;
    s_screens[SCREEN_ORIENT]   = &orient_screen;

    for (int i = 0; i < 2; i++) {
        s_buf[i] = heap_caps_malloc(LCD_STRIP_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!s_buf[i]) {
            ESP_LOGE(TAG, "sem memoria DMA para buffer de tira %d", i);
            return;
        }
        memset(s_buf[i], 0, LCD_STRIP_BYTES);
    }

    s_touch_q = xQueueCreate(8, sizeof(touch_evt_t));
    s_ctrl_q = xQueueCreate(4, sizeof(ctrl_msg_t));

    xTaskCreatePinnedToCore(touch_task, "touch", TOUCH_TASK_STACK, NULL,
                            PRIO_TOUCH, NULL, CORE_GAME);

    ui_dirty_all();
    ESP_LOGI(TAG, "engine pronta (%d tiras de %d px, buffers 2x%d B)",
             N_STRIPS, LCD_STRIP_LINES, LCD_STRIP_BYTES);
}
