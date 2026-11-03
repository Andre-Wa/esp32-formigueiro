/*
 * weather.h — Clima do habitat.
 *
 * Com WiFi: consulta a API aberta Open-Meteo (sem chave) para a cidade
 * configurada e converte o código WMO em um dos estados abaixo.
 * Sem WiFi: simula um clima plausível (Markov suave) para o jogo nunca
 * ficar sem estação do ano.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* estados de clima do jogo */
enum {
    WX_SUN = 0,     /* céu limpo            */
    WX_PARTLY,      /* parcialmente nublado */
    WX_CLOUD,       /* nublado              */
    WX_FOG,         /* névoa                 */
    WX_RAIN,        /* chuva                 */
    WX_STORM,       /* tormenta              */
    WX_SNOW,        /* frio extremo/neve     */
};

/* converte código WMO (Open-Meteo) -> estado do jogo */
uint8_t weather_wmo_to_wx(int code);

/* parse da resposta JSON do /forecast (chamado pela tarefa de rede) */
bool weather_parse_forecast(const char *json, uint8_t *wx, int16_t *temp_x10,
                            uint8_t *humidity, uint8_t *precip_prob);

/* parse da resposta JSON do /search (geocoding) */
bool weather_parse_geocode(const char *json, int16_t *lat_x100, int16_t *lon_x100);

/* clima simulado (sem rede): sorteia o próximo estado a partir do atual */
void weather_simulate(void);

const char *weather_name(uint8_t wx);
