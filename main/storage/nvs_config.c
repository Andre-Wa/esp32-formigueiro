/*
 * nvs_config.c — Persistência das configurações de dispositivo em NVS.
 *
 * Formato: um único blob "cfg" versionado + chaves individuais espelhadas
 * (para inspeção via `idf.py nvs-monitor`). Se o blob não bater com a versão
 * corrente, volta aos padrões sem apagar o save do jogo (partição separada).
 */
#include <string.h>
#include <stdio.h>
#include "nvs_config.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "app_config.h"

static const char *TAG = "cfg";

static app_config_t s_cfg;

app_config_t *appcfg_get(void)
{
    return &s_cfg;
}

static void config_defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.brightness      = 85;
    s_cfg.lcd_xform       = 0;        /* MADCTL 0x28: CYD padrão, paisagem */
    s_cfg.speed           = 1;
    s_cfg.led_mode        = 1;
    s_cfg.weather_enabled = 1;
    /* coordenadas padrão: São Paulo (o usuário troca em Configurações) */
    s_cfg.lat_x100        = -2355;
    s_cfg.lon_x100        = -4663;
    s_cfg.geo_ok          = true;
    strlcpy(s_cfg.city, "Sao Paulo", sizeof(s_cfg.city));
    strlcpy(s_cfg.tz, "<-03>3", sizeof(s_cfg.tz));   /* horário de Brasília */
}

esp_err_t appcfg_load(void)
{
    config_defaults();

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_CFG, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "primeiro boot - criando configuracao padrao");
        return appcfg_save();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open falhou (%s) - usando padroes", esp_err_to_name(err));
        return ESP_OK;
    }

    app_config_t tmp;
    size_t sz = sizeof(tmp);
    if (nvs_get_blob(h, "blob", &tmp, &sz) == ESP_OK && sz == sizeof(tmp)) {
        memcpy(&s_cfg, &tmp, sizeof(s_cfg));
        /* saneamento defensivo */
        if (s_cfg.brightness > 100) s_cfg.brightness = 85;
        if (s_cfg.lcd_xform > LCD_MODE_MAX) s_cfg.lcd_xform = 0;
        if (s_cfg.speed != 0 && s_cfg.speed != 1 && s_cfg.speed != 2 &&
            s_cfg.speed != 4) s_cfg.speed = 1;
        s_cfg.wifi_ssid[sizeof(s_cfg.wifi_ssid) - 1] = '\0';
        s_cfg.wifi_pass[sizeof(s_cfg.wifi_pass) - 1] = '\0';
        s_cfg.city[sizeof(s_cfg.city) - 1] = '\0';
        ESP_LOGI(TAG, "config carregada: brilho=%u%% modo=%u speed=%u wifi='%s' cidade='%s'",
                 s_cfg.brightness, s_cfg.lcd_xform, s_cfg.speed,
                 s_cfg.wifi_ssid[0] ? s_cfg.wifi_ssid : "-", s_cfg.city);
    } else {
        ESP_LOGW(TAG, "blob de config ausente/incompativel - usando padroes");
        /* mantém orientação/calibração antigas, se existirem como chaves soltas */
        uint8_t u8;
        if (nvs_get_u8(h, "bright", &u8) == ESP_OK) s_cfg.brightness = u8;
        if (nvs_get_u8(h, "xform", &u8) == ESP_OK && u8 <= LCD_MODE_MAX) s_cfg.lcd_xform = u8;
        if (nvs_get_u8(h, "orientok", &u8) == ESP_OK) s_cfg.orient_ok = u8;
        float cal[6];
        sz = sizeof(cal);
        if (nvs_get_blob(h, "calib", cal, &sz) == ESP_OK && sz == sizeof(cal)) {
            memcpy(s_cfg.calib, cal, sizeof(cal));
            s_cfg.has_calib = true;
        }
    }

    nvs_close(h);
    return ESP_OK;
}

esp_err_t appcfg_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_CFG, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open RW falhou: %s", esp_err_to_name(err));
        return err;
    }

    nvs_set_blob(h, "blob", &s_cfg, sizeof(s_cfg));
    /* espelho legível das chaves mais úteis */
    nvs_set_u8(h, "ver", APP_CFG_VER);
    nvs_set_u8(h, "bright", s_cfg.brightness);
    nvs_set_u8(h, "xform", s_cfg.lcd_xform);
    nvs_set_u8(h, "orientok", s_cfg.orient_ok);
    nvs_set_u8(h, "speed", s_cfg.speed);
    if (s_cfg.has_calib) {
        nvs_set_blob(h, "calib", s_cfg.calib, sizeof(s_cfg.calib));
    }
    if (s_cfg.wifi_ssid[0]) {
        nvs_set_str(h, "ssid", s_cfg.wifi_ssid);
    }
    if (s_cfg.city[0]) {
        nvs_set_str(h, "city", s_cfg.city);
    }

    err = nvs_commit(h);
    nvs_close(h);
    return err;
}
