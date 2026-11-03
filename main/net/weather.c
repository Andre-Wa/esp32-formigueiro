/*
 * weather.c — conversão WMO, parsing Open-Meteo e clima simulado offline.
 */
#include <string.h>
#include <stdio.h>
#include "cJSON.h"
#include "weather.h"
#include "colony.h"

uint8_t weather_wmo_to_wx(int code)
{
    if (code == 0 || code == 1) return WX_SUN;
    if (code == 2) return WX_PARTLY;
    if (code == 3) return WX_CLOUD;
    if (code >= 45 && code <= 48) return WX_FOG;
    if (code >= 51 && code <= 67) return WX_RAIN;
    if (code >= 71 && code <= 77) return WX_SNOW;
    if (code >= 80 && code <= 82) return WX_RAIN;
    if (code >= 85 && code <= 86) return WX_SNOW;
    if (code >= 95) return WX_STORM;
    return WX_PARTLY;
}

bool weather_parse_forecast(const char *json, uint8_t *wx, int16_t *temp_x10,
                            uint8_t *humidity, uint8_t *precip_prob)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;

    cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "current");
    if (!cJSON_IsObject(cur)) { cJSON_Delete(root); return false; }

    cJSON *t = cJSON_GetObjectItemCaseSensitive(cur, "temperature_2m");
    cJSON *h = cJSON_GetObjectItemCaseSensitive(cur, "relative_humidity_2m");
    cJSON *w = cJSON_GetObjectItemCaseSensitive(cur, "weather_code");
    if (!cJSON_IsNumber(t) || !cJSON_IsNumber(w)) { cJSON_Delete(root); return false; }

    *temp_x10 = (int16_t)(t->valuedouble * 10.0);
    *wx = weather_wmo_to_wx((int)w->valueint);
    *humidity = cJSON_IsNumber(h) ? (uint8_t)h->valueint : 50;

    /* probabilidade de chuva: pico das próximas horas */
    uint8_t pp = 0;
    cJSON *hourly = cJSON_GetObjectItemCaseSensitive(root, "hourly");
    if (cJSON_IsObject(hourly)) {
        cJSON *arr = cJSON_GetObjectItemCaseSensitive(hourly, "precipitation_probability");
        if (cJSON_IsArray(arr)) {
            int i = 0;
            cJSON *it = NULL;
            cJSON_ArrayForEach(it, arr) {
                if (i++ > 12) break;
                if (cJSON_IsNumber(it) && it->valueint > pp) pp = (uint8_t)it->valueint;
            }
        }
    }
    *precip_prob = pp;
    cJSON_Delete(root);
    return true;
}

bool weather_parse_geocode(const char *json, int16_t *lat_x100, int16_t *lon_x100)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    cJSON *res = cJSON_GetObjectItemCaseSensitive(root, "results");
    bool ok = false;
    if (cJSON_IsArray(res) && cJSON_GetArraySize(res) > 0) {
        cJSON *first = cJSON_GetArrayItem(res, 0);
        cJSON *la = cJSON_GetObjectItemCaseSensitive(first, "latitude");
        cJSON *lo = cJSON_GetObjectItemCaseSensitive(first, "longitude");
        if (cJSON_IsNumber(la) && cJSON_IsNumber(lo)) {
            *lat_x100 = (int16_t)(la->valuedouble * 100.0);
            *lon_x100 = (int16_t)(lo->valuedouble * 100.0);
            ok = true;
        }
    }
    cJSON_Delete(root);
    return ok;
}

/* ---------------- clima simulado (offline) ---------------- */
void weather_simulate(void)
{
    uint8_t cur = g_col.weather;
    uint32_t r = colony_rand_range(100);
    uint8_t next;
    switch (cur) {
    case WX_SUN:    next = r < 60 ? WX_SUN : r < 85 ? WX_PARTLY : r < 95 ? WX_CLOUD : WX_RAIN; break;
    case WX_PARTLY: next = r < 40 ? WX_SUN : r < 70 ? WX_PARTLY : r < 90 ? WX_CLOUD : WX_RAIN; break;
    case WX_CLOUD:  next = r < 25 ? WX_PARTLY : r < 60 ? WX_CLOUD : r < 90 ? WX_RAIN : WX_STORM; break;
    case WX_RAIN:   next = r < 40 ? WX_RAIN : r < 60 ? WX_STORM : r < 90 ? WX_CLOUD : WX_PARTLY; break;
    case WX_STORM:  next = r < 50 ? WX_STORM : r < 80 ? WX_RAIN : WX_CLOUD; break;
    case WX_FOG:    next = r < 50 ? WX_FOG : r < 80 ? WX_CLOUD : WX_PARTLY; break;
    default:        next = WX_PARTLY; break;
    }
    g_col.weather = next;

    static const int16_t base[] = { 260, 245, 225, 205, 195, 185, 120 };
    g_col.temp_x10 = (int16_t)(base[next] + (int16_t)colony_rand_range(60) - 30);
    static const uint8_t hum[] = { 45, 55, 65, 85, 90, 95, 80 };
    g_col.humidity = hum[next];
    colony_event("Clima mudou: %s.", weather_name(next));
}

const char *weather_name(uint8_t wx)
{
    switch (wx) {
    case WX_SUN:    return "sol";
    case WX_PARTLY: return "parcialmente nublado";
    case WX_CLOUD:  return "nublado";
    case WX_FOG:    return "nevoa";
    case WX_RAIN:   return "chuva";
    case WX_STORM:  return "tormenta";
    case WX_SNOW:   return "frio extremo";
    default:        return "-";
    }
}
