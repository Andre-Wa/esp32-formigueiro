#include "ldr.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "app_config.h"

static const char *TAG = "ldr";
static adc_oneshot_unit_handle_t s_h;

void ldr_init(void)
{
    adc_oneshot_unit_init_cfg_t uc = { .unit_id = ADC_UNIT_1 };
    if (adc_oneshot_new_unit(&uc, &s_h) != ESP_OK) {
        s_h = NULL;
        ESP_LOGW(TAG, "ADC1 indisponivel - brilho automatico desativado");
        return;
    }
    adc_oneshot_chan_cfg_t cc = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_oneshot_config_channel(s_h, LDR_ADC_CHAN, &cc) != ESP_OK) {
        s_h = NULL;
        ESP_LOGW(TAG, "canal do LDR falhou");
    }
}

int ldr_brightness(void)
{
    if (!s_h) return -1;

    int acc = 0, n = 0;
    for (int i = 0; i < 4; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_h, LDR_ADC_CHAN, &raw) == ESP_OK) { acc += raw; n++; }
    }
    if (!n) return -1;
    int raw = acc / n;

    int pct = raw * 100 / 4095;
#if LDR_INVERT
    pct = 100 - pct;
#endif
    int b = 12 + pct * 88 / 100;      /* 12% no escuro .. 100% na luz */
    if (b < 10) b = 10;
    if (b > 100) b = 100;
    return b;
}
