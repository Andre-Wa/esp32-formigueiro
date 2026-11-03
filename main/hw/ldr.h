/*
 * ldr.[ch] — sensor de luminosidade do CYD (GPIO34) para brilho automático.
 *
 * O LDR do CYD forma um divisor com a luz ambiente: mais luz => tensão maior
 * no GPIO34 (se na sua placa for o contrário, inverta LDR_INVERT).
 */
#pragma once

void ldr_init(void);

/* Brilho alvo 10..100 lido do sensor, ou -1 se o ADC não inicializou. */
int ldr_brightness(void);
