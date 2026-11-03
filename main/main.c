/*
 * main.c — Ponto de entrada do Formigueiro Virtual (ESP32-2432S028R).
 *
 *   Core 0: tarefa de rede (WiFi/NTP/clima)
 *   Core 1: tarefa do jogo (simulação + render) + tarefa de touch
 *
 * Boot: NVS -> save da partição "save" -> LCD/touch -> UI -> rede -> jogo.
 */
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_event.h"

#include "app_config.h"
#include "app_types.h"
#include "storage/nvs_config.h"
#include "storage/game_save.h"
#include "display/lcd_spi_driver.h"
#include "display/touch_xpt2046.h"
#include "ui/ui_engine.h"
#include "ui/screens/screens_common.h"
#include "game/colony.h"
#include "hw/led.h"
#include "hw/sfx.h"
#include "net/wifi_prov.h"
#include "net/weather.h"

static const char *TAG = "main";

QueueHandle_t g_cmd_queue;
QueueHandle_t g_evt_queue;

extern void game_screen_refresh(void);   /* ui/screens/game_screen.c */

static bool s_boot_ui_done;
static bool s_offline_applied;

/* ================= comandos de sistema (fora da simulação) =============== */
static void handle_system_cmd(const app_cmd_t *c)
{
    app_config_t *cfg = appcfg_get();
    switch (c->id) {
    case CMD_SET_BRIGHTNESS:
        cfg->brightness = (uint8_t)(c->val < 10 ? 10 : c->val > 100 ? 100 : c->val);
        lcd_set_backlight(cfg->brightness);
        appcfg_save();
        ui_dirty_rect(0, 0, LCD_H_RES, 24);
        break;
    case CMD_SET_CLOCK:
        g_col.sec_of_day = (g_col.sec_of_day + 3600u * (uint32_t)c->val) % 86400u;
        colony_msg("Hora ajustada para %02d:%02d.", colony_hour(), colony_minute());
        break;
    case CMD_SET_CITY:
        if (c->str[0]) net_request_geocode(c->str);
        break;
    case CMD_START_PROVISION:
        net_start_provision();
        break;
    case CMD_RESET_GAME:
        game_save_erase();
        colony_new();
        s_offline_applied = true;
        ui_show(SCREEN_GAME);
        ui_invalidate_all();
        break;
    default:
        break;
    }
}

/* ================= eventos de sistema -> colônia ========================= */
static void handle_evt(const app_evt_t *e)
{
    switch (e->id) {
    case EVT_WEATHER:
        g_col.weather = (uint8_t)e->val;
        g_col.temp_x10 = (int16_t)e->val2;
        g_col.humidity = e->b1;
        g_col.precip_prob = e->b2;
        g_col.net_ok = 1;
        g_col.weather_age_min = 0;
        colony_event("Clima em %s: %s.", e->str[0] ? e->str : "?",
                     weather_name(g_col.weather));
        break;
    case EVT_WIFI_STATE:
        g_col.net_ok = (e->val == WIFI_ONLINE) ? 1 : 0;
        break;
    case EVT_TIME_SYNC: {
        time_t now = (time_t)e->val + 1700000000L;
        struct tm tm;
        localtime_r(&now, &tm);
        g_col.sec_of_day = (uint32_t)(tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec);
        g_col.time_sync = 1;
        if (!s_offline_applied) {
            s_offline_applied = true;
            if (g_col.epoch_save > 0 && now > g_col.epoch_save) {
                colony_offline((uint32_t)(now - g_col.epoch_save));
            }
        }
        break;
    }
    case EVT_TOAST:
        colony_msg("%s", e->str);
        break;
    default:
        break;
    }
}

/* ================= tarefa do jogo (Core 1) =============================== */
static void game_task(void *arg)
{
    int64_t last = esp_timer_get_time() / 1000;
    int64_t boot = last;
    TickType_t xLastWake = xTaskGetTickCount();

    while (true) {
        int64_t now = esp_timer_get_time() / 1000;
        int elapsed = (int)(now - last);
        last = now;

        /* 1) comandos do touch */
        app_cmd_t cmd;
        while (xQueueReceive(g_cmd_queue, &cmd, 0) == pdTRUE) {
            switch (cmd.id) {
            case CMD_SET_BRIGHTNESS:
            case CMD_SET_CLOCK:
            case CMD_SET_CITY:
            case CMD_START_PROVISION:
            case CMD_RESET_GAME:
                handle_system_cmd(&cmd);
                break;
            default:
                colony_command(&cmd);
                break;
            }
        }

        /* 2) eventos de rede/sistema */
        app_evt_t ev;
        while (xQueueReceive(g_evt_queue, &ev, 0) == pdTRUE) {
            handle_evt(&ev);
        }

        /* 3) fluxo de boot das telas */
        if (!s_boot_ui_done && now - boot > 1500) {
            s_boot_ui_done = true;
            app_config_t *cfg = appcfg_get();
            if (DEBUG_CALIB_EVERY_BOOT)      ui_show(SCREEN_ORIENT);
            else if (!cfg->orient_ok)        ui_show(SCREEN_ORIENT);
            else if (!cfg->has_calib)        ui_show(SCREEN_CALIB);
            else                             ui_show(SCREEN_GAME);
        }

        /* 4) simulação + render parcial */
        colony_step(elapsed);
        game_screen_refresh();
        ui_pump();

        /* 5) LED de status */
        if ((now / 200) != ((now - elapsed) / 200)) led_update();

        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(COLONY_TICK_MS));
    }
}

/* ================= app_main ============================================== */
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " %s v%s — habitat virtual de formigas", APP_NAME, APP_VERSION);
    ESP_LOGI(TAG, " ESP32-2432S028R | heap inicial: %u B",
             (unsigned)esp_get_free_heap_size());
    ESP_LOGI(TAG, "========================================");

    /* NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(appcfg_load());

    /* rede: bases do ESP-IDF (a tarefa de rede sobe depois do display) */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* filas */
    g_cmd_queue = xQueueCreate(12, sizeof(app_cmd_t));
    g_evt_queue = xQueueCreate(8, sizeof(app_evt_t));
    configASSERT(g_cmd_queue && g_evt_queue);

    led_init();
    sfx_init();

    /* display + touch + UI (splash imediato) */
    ESP_ERROR_CHECK(lcd_init());
    ESP_ERROR_CHECK(xpt2046_init());
    if (appcfg_get()->has_calib) xpt2046_set_calibration(appcfg_get()->calib);
    ui_init();

    /* save do jogo */
    bool have_save = (game_save_init() == ESP_OK);
    size_t len = 0;
    const void *buf = game_save_data(&len);
    if (!have_save || !colony_load(buf, len)) {
        colony_new();
        ESP_LOGI(TAG, "colonia nova criada");
    } else {
        ESP_LOGI(TAG, "colonia restaurada do save (dia %lu, %u operarias)",
                 (unsigned long)g_col.day, g_col.n_workers);
    }

    /* rede (WiFi/NTP/clima) no Core 0 */
    net_init();

    /* tarefa do jogo */
    xTaskCreatePinnedToCore(game_task, "game", GAME_TASK_STACK, NULL,
                            PRIO_GAME, NULL, CORE_GAME);

    ESP_LOGI(TAG, "heap apos init: %u B", (unsigned)esp_get_free_heap_size());
}
