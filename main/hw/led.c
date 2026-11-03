/*
 * led.c — LED RGB (ânodo comum) do CYD como semáforo do habitat.
 */
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "app_config.h"
#include "nvs_config.h"
#include "colony.h"
#include "net/wifi_prov.h"
#include "led.h"

static void set(bool r, bool g, bool b)
{
#if LED_ACTIVE_LOW
    gpio_set_level(LED_PIN_R, !r);
    gpio_set_level(LED_PIN_G, !g);
    gpio_set_level(LED_PIN_B, !b);
#else
    gpio_set_level(LED_PIN_R, r);
    gpio_set_level(LED_PIN_G, g);
    gpio_set_level(LED_PIN_B, b);
#endif
}

void led_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << LED_PIN_R) | (1ULL << LED_PIN_G) | (1ULL << LED_PIN_B),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    set(false, false, false);
}

void led_update(void)
{
    static uint8_t tick;
    tick++;
    bool on = (tick % 4) < 2;          /* piscado ~1,2 Hz */
    bool breath = (tick % 8) < 5;      /* respiração lenta */

    if (!appcfg_get()->led_mode) { set(false, false, false); return; }

    if (g_col.flash_ticks) { set(true, true, false); return; }

    wifi_state_t ws = net_state();
    if (ws == WIFI_CONNECTING || ws == WIFI_STARTING) { set(0, 0, on); return; }
    if (ws == WIFI_AP_PORTAL)                        { set(0, on, on); return; }

    switch (g_col.alarm) {
    case 1: set(on, on, 0); return;                  /* âmbar: fome */
    case 2: set(on, 0, 0); return;                   /* vermelho: perigo */
    case 3: set(on, 0, on); return;                  /* roxo: tormenta */
    default: break;
    }
    if (colony_is_night()) { set(0, 0, breath); return; }   /* azulinho: noite */
    set(0, breath, 0);                               /* verde respirando */
}
