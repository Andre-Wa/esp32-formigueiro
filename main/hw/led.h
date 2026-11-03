/*
 * led.h — LED RGB de status da placa como "semáforo do habitat".
 *
 *   verde respirando  : rainha feliz, tudo em ordem
 *   âmbar             : rainha com fome / despensa vazia
 *   vermelho piscando : saúde baixa ou predador
 *   azul piscando     : WiFi conectando / portal aberto
 *   roxo suave        : noite (colônia dormindo)
 *   piscada verde     : ovo posto / obra concluída (flash_ticks)
 */
#pragma once

void led_init(void);
void led_update(void);      /* chame ~5x por segundo a partir da task do jogo */
