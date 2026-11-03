/*
 * nvs_config.h — Configurações de DISPOSITIVO persistentes (NVS).
 *
 * O estado do jogo (colônia, rainha, operárias, estoques) NÃO fica aqui: vai
 * para a partição "save" dedicada, com dois setores alternados + CRC32
 * (ver storage/game_save.h). Assim a NVS fica pequena e o save pode ser
 * gravado com frequência sem desgastar a NVS.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "lcd_spi_driver.h"   /* LCD_MODE_MAX */

typedef struct {
    /* display / touch */
    uint8_t  brightness;         /* backlight 0..100 */
    uint8_t  lcd_xform;          /* modo de orientação 0..LCD_MODE_MAX */
    uint8_t  orient_ok;          /* orientação já confirmada pelo usuário */
    bool     has_calib;
    float    calib[6];           /* xs = c0*rx + c1*ry + c2 ; ys = c3*..+c5 */

    /* rede */
    char     wifi_ssid[33];
    char     wifi_pass[65];
    bool     wifi_ok;
    char     city[25];           /* cidade p/ geocoding (clima) */
    int16_t  lat_x100;           /* latitude  x100 (ex.: -2355 = -23.55) */
    int16_t  lon_x100;           /* longitude x100 */
    bool     geo_ok;
    uint8_t  weather_enabled;    /* 1 = consulta o clima (padrão) */
    char     tz[24];             /* fuso POSIX p/ NTP (ex. "<-03>3") */

    /* jogo */
    uint8_t  speed;              /* 0=pausa 1=normal 2=rapido 4=turbo */
    uint8_t  sound;              /* reservado (módulo de áudio futuro) */
    uint8_t  led_mode;           /* 0=estado da colonia 1=off */
    int32_t  clock_offset_s;     /* correção manual de hora (sem NTP) */
} app_config_t;

#define APP_CFG_VER 1
#define NVS_NS_CFG  "cfg"

esp_err_t appcfg_load(void);
esp_err_t appcfg_save(void);
app_config_t *appcfg_get(void);
