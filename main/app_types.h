/*
 * app_types.h — Tipos compartilhados, comandos de usuário e eventos de sistema.
 *
 *   g_cmd_queue   : UI (touch) -> tarefa do jogo (Core 1)
 *   g_evt_queue   : rede/sistema -> tarefa do jogo (clima, hora, WiFi)
 *
 * Toda mutação do estado da colônia acontece NA tarefa do jogo: assim não é
 * preciso mutex e a simulação fica determinística.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* ============================ Comandos do usuário ========================= */
typedef enum {
    CMD_NONE = 0,
    /* tela/tabs */
    CMD_VIEW_COLONY,        /* mostra o interior do formigueiro */
    CMD_VIEW_OUTSIDE,       /* mostra o exterior */
    CMD_OPEN_MENU,          /* menu radial de ações da rainha */
    CMD_CLOSE_MENU,
    CMD_OPEN_SETTINGS,
    /* ações diretas na rainha (botões fixos / menu radial) */
    CMD_PET,                /* acariciar */
    CMD_FEED,               /* alimentar */
    CMD_PLAY,               /* brincar */
    CMD_GROOM,              /* limpar/cuidar */
    CMD_REST,               /* descansar */
    CMD_HUNT,               /* expedição de caça */
    CMD_BUILD,              /* trabalhar: escavar/expandir câmara */
    CMD_TEND,               /* trabalhar: cuidar do berçário */
    CMD_FORAGE,             /* trabalhar: mandar operárias coletar */
    CMD_LAY_EGG,            /* pedir postura (se as condições permitirem) */
    /* alvos no cenário */
    CMD_TAP_CHAMBER,        /* val = índice da câmara tocada */
    CMD_TAP_WORKER,         /* val = índice da operária tocada */
    CMD_TAP_RESOURCE,       /* val = índice do recurso no exterior */
    CMD_TAP_GROUND,         /* x/y de mundo: manda uma batedora até lá */
    /* sistema */
    CMD_SET_SPEED,          /* val = 0 (pausa) / 1 / 2 / 4 */
    CMD_SET_BRIGHTNESS,     /* val = 0..100 */
    CMD_CALIB_DONE,         /* calibração de touch concluída */
    CMD_SAVE_NOW,
    CMD_RESET_GAME,         /* apaga o save e recomeça a colônia */
    CMD_SET_CITY,           /* str = nome da cidade (geocoding) */
    CMD_START_PROVISION,    /* liga o portal SoftAP de WiFi */
    CMD_SET_CLOCK,          /* val = horas (0..23) — ajuste manual */
} cmd_id_t;

typedef struct {
    cmd_id_t id;
    int32_t  val;
    int32_t  val2;
    char     str[48];
} app_cmd_t;

/* ============================ Eventos de sistema ========================== */
typedef enum {
    EVT_NONE = 0,
    EVT_WEATHER,        /* val = weather_code, val2 = temp*10; str = cidade */
    EVT_TIME_SYNC,      /* val = epoch UTC (s) */
    EVT_WIFI_STATE,     /* val = wifi_state_t */
    EVT_NET_ERROR,      /* str = mensagem */
    EVT_TOAST,          /* str = mensagem para o usuário */
} evt_id_t;

typedef enum {
    WIFI_OFF = 0,
    WIFI_STARTING,
    WIFI_CONNECTING,
    WIFI_ONLINE,
    WIFI_AP_PORTAL,     /* portal de configuração ativo */
    WIFI_FAILED,
} wifi_state_t;

typedef struct {
    evt_id_t id;
    int32_t  val;
    int32_t  val2;
    uint8_t  b1, b2;      /* campos auxiliares (umidade, prob. chuva...) */
    char     str[64];
} app_evt_t;

/* Filas globais (criadas em main.c) */
extern QueueHandle_t g_cmd_queue;
extern QueueHandle_t g_evt_queue;

static inline void app_send_cmd(cmd_id_t id, int32_t val)
{
    app_cmd_t c = { .id = id, .val = val };
    if (g_cmd_queue) {
        xQueueSend(g_cmd_queue, &c, 0);
    }
}

static inline void app_send_cmd_str(cmd_id_t id, const char *s)
{
    app_cmd_t c = { .id = id };
    if (s) {
        size_t i = 0;
        for (; s[i] && i < sizeof(c.str) - 1; i++) {
            c.str[i] = s[i];
        }
        c.str[i] = '\0';
    }
    if (g_cmd_queue) {
        xQueueSend(g_cmd_queue, &c, 0);
    }
}

static inline void app_post_evt(const app_evt_t *e)
{
    if (g_evt_queue) {
        xQueueSend(g_evt_queue, e, pdMS_TO_TICKS(200));
    }
}
