/*
 * wifi_prov.h — WiFi do habitat: estação (clima/hora) + portal SoftAP para
 * configurar as credenciais pelo celular (página web servida pela placa).
 */
#pragma once

#include "app_types.h"

/* Cria a tarefa de rede (Core 0). Chame depois de appcfg_load(). */
void net_init(void);

/* Liga o portal de configuração (SoftAP + página web). */
void net_start_provision(void);

wifi_state_t net_state(void);

/* SSID do portal (ex.: "Formigueiro-A1B2"). */
const char *wifi_ap_ssid(void);

/* Força uma consulta de clima na próxima rodada da tarefa. */
void net_request_weather(void);

/* Pede geocoding da cidade (atualiza lat/lon na config). */
void net_request_geocode(const char *city);
