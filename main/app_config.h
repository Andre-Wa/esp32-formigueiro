/*
 * app_config.h — Mapeamento de pinos do ESP32-2432S028R (Cheap Yellow Display)
 * e constantes globais do FORMIGUEIRO VIRTUAL.
 *
 * Distribuição dos periféricos SPI (o ESP32 possui apenas 2 hosts SPI de uso
 * geral, mas a placa expõe 3 conjuntos de pinos SPI):
 *
 *   SPI2_HOST (HSPI, pinos nativos) -> LCD ILI9341  (DMA + render por tiras)
 *   SPI3_HOST (VSPI)                -> livre (cartão SD, se você quiser usar)
 *   Software SPI (bit-bang)         -> Touch XPT2046 (baixa taxa, 50 Hz)
 *
 * Este projeto NÃO usa Bluetooth nem áudio (o GPIO26 fica livre para um
 * buzzer/módulo de som no futuro — ver hw/sfx.h).
 */
#pragma once

#include "driver/spi_common.h"
#include "driver/gpio.h"

/* ===================== Display ILI9341 320x240 (SPI2) ===================== */
#define LCD_SPI_HOST        SPI2_HOST
#define LCD_PIN_MOSI        GPIO_NUM_13
#define LCD_PIN_MISO        GPIO_NUM_12
#define LCD_PIN_SCLK        GPIO_NUM_14
#define LCD_PIN_CS          GPIO_NUM_15
#define LCD_PIN_DC          GPIO_NUM_2
#define LCD_PIN_BL          GPIO_NUM_21     /* backlight via LEDC (PWM) */
#define LCD_PIN_RST         GPIO_NUM_NC     /* sem reset por hardware */

#define LCD_H_RES           320
#define LCD_V_RES           240
/* 40 MHz com CS/DC manuais (modelo TFT_eSPI/Adafruit, comprovado no CYD).
 * Se houver instabilidade de escrita em cabos/unidades marginais, reduza
 * para 20 MHz. */
#define LCD_PCLK_HZ         (40 * 1000 * 1000)

/* MADCTL usado apenas como referência de log; a orientação real vem da NVS
 * (lcd_xform), ajustável na tela "Orientação" no primeiro boot.
 *   0x28 = MV|BGR  (padrão CYD: paisagem, USB à esquerda, cores BGR) */
#define LCD_MADCTL          0x28

/* Inversão de cores do painel. ILI9341 do CYD: 0 (INVOFF).
 * Se as cores parecerem "negativo fotográfico", mude para 1. */
#define LCD_INVERT_COLORS   0

/* Renderização parcial por tiras: 320 x 16 x RGB565 = 10240 bytes por buffer.
 * Dois buffers (ping-pong) = 20,5 KB de SRAM interna com capacidade DMA. */
#define LCD_STRIP_LINES     16
#define LCD_STRIP_BYTES     (LCD_H_RES * LCD_STRIP_LINES * 2)

/* 0 = sem hardware de display (build para emulador/testes de lógica) */
#ifndef LCD_HW_PRESENT
#define LCD_HW_PRESENT      1
#endif

/* ===================== Touch XPT2046 (SPI software) ======================= */
#define TOUCH_PIN_CLK       GPIO_NUM_25
#define TOUCH_PIN_MOSI      GPIO_NUM_32
#define TOUCH_PIN_MISO      GPIO_NUM_39     /* input-only */
#define TOUCH_PIN_CS        GPIO_NUM_33
#define TOUCH_PIN_IRQ       GPIO_NUM_36     /* input-only; usamos polling */

#define TOUCH_ADC_MAX       4096            /* 12 bits */
#define TOUCH_Z_THRESHOLD   600             /* pressão mínima (placa CYD) */

/* Sensor de luz (LDR) do CYD: GPIO34 = ADC1_CH6 */
#define LDR_GPIO            34
#define LDR_ADC_CHAN        6               /* ADC1_CHANNEL_6 */
#define LDR_INVERT          0               /* 1 se o seu painel ler ao contrário */
#define TOUCH_SAMPLES_AVG   3

/* Mapeamento padrão bruto -> tela (paisagem, MADCTL 0x28).
 * A calibração de 4 pontos (tela "Calibrar") refina/sobrepõe estes valores. */
#define TOUCH_SWAP_XY       1               /* eixo Y bruto vira X da tela */
#define TOUCH_MIRROR_X      1
#define TOUCH_MIRROR_Y      1

/* ===================== Cartão SD (SPI3) — não usado ======================= */
#define SD_SPI_HOST         SPI3_HOST
#define SD_PIN_CS           GPIO_NUM_5
#define SD_PIN_MOSI         GPIO_NUM_23
#define SD_PIN_MISO         GPIO_NUM_19
#define SD_PIN_SCLK         GPIO_NUM_18

/* ===================== LED RGB de status (ânodo comum) ==================== */
#define LED_PIN_R           GPIO_NUM_4
#define LED_PIN_G           GPIO_NUM_16
#define LED_PIN_B           GPIO_NUM_17
#define LED_ACTIVE_LOW      1               /* nível 0 = aceso */

/* ===================== Expansões futuras (áudio / vibração) =============== */
/* Pinos LIVRES no conector P3 do CYD. Nada é inicializado enquanto
 * SFX_ENABLE / HAPTIC_ENABLE estiverem em 0 — é só ligar o módulo e mudar a
 * constante (ver hw/sfx.h). */
#define SFX_PIN_BUZZER      GPIO_NUM_26     /* DAC1 / amp SC8002D on-board */
#define SFX_ENABLE          0               /* 1 = habilita sons do jogo */
#define HAPTIC_PIN_MOTOR    GPIO_NUM_22     /* motor de vibração via MOSFET */
#define HAPTIC_ENABLE       0

/* ===================== Rede (clima + hora) ================================ */
#define WIFI_AP_SSID_PREFIX "Formigueiro"   /* SSID do portal de configuração */
#define WIFI_AP_PASS        "formiga123"    /* senha do portal (WPA2) */
#define WIFI_MAX_RETRY      6

/* Open-Meteo: sem chave de API. Use https:// (root CA embutido) ou http://. */
#define WEATHER_URL_FMT \
    "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f" \
    "&current=temperature_2m,relative_humidity_2m,precipitation,weather_code,wind_speed_10m" \
    "&hourly=precipitation_probability&forecast_days=1&timezone=auto"
#define GEO_URL_FMT \
    "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=pt&format=json"
#define WEATHER_REFRESH_MIN   20            /* minutos entre consultas */
#define NTP_SERVER            "pool.ntp.org"

/* ===================== Aplicação ========================================== */
#define APP_NAME            "Formigueiro"
#define APP_VERSION         "0.3.3"

/* Força o fluxo de orientação + calibração de touch em TODO boot (debug). */
#define DEBUG_CALIB_EVERY_BOOT 0

/* Prioridades de tarefas */
#define PRIO_GAME           6               /* sim + render (Core 1) */
#define PRIO_TOUCH          7               /* acima do game: amostra mesmo em quadros longos */
#define PRIO_NET            4               /* WiFi/clima/NTP (Core 0) */

/* Núcleos */
#define CORE_NET            0
#define CORE_GAME           1

/* Memória */
#define GAME_TASK_STACK     8192
#define NET_TASK_STACK      6144
#define TOUCH_TASK_STACK    2560
