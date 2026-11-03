/*
 * wifi_prov.c — estação WiFi + portal SoftAP de configuração + NTP + clima.
 *
 * Fluxo do portal: Configurações → WIFI → a placa vira um access point
 * "Formigueiro-XXXX" (senha: formiga123). Pelo celular, abra qualquer
 * página (o portal redireciona) ou acesse 192.168.4.1, informe SSID/senha
 * e a placa salva na NVS e conecta sozinha.
 *
 * Com internet: SNTP acerta o relógio (rotina dia/noite real) e a API
 * Open-Meteo alimenta o clima do habitat (sem chave de API).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "cJSON.h"

#include "app_config.h"
#include "app_types.h"
#include "nvs_config.h"
#include "wifi_prov.h"
#include "weather.h"

static const char *TAG = "net";

static volatile wifi_state_t s_state = WIFI_OFF;
static char s_ap_ssid[28];
static httpd_handle_t s_httpd;
static bool s_prov_req;
static bool s_apply_cred;
static char s_new_ssid[33], s_new_pass[65];
static bool s_weather_req;
static char s_geo_req[25];
static bool s_sntp_done;
static int  s_retries;
static int64_t s_last_weather_ms;
static esp_netif_t *s_ap_netif;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

wifi_state_t net_state(void) { return s_state; }
const char *wifi_ap_ssid(void) { return s_ap_ssid; }
void net_request_weather(void) { s_weather_req = true; }
void net_request_geocode(const char *city)
{
    strlcpy(s_geo_req, city, sizeof(s_geo_req));
}

static void set_state(wifi_state_t st)
{
    s_state = st;
    app_evt_t e = { .id = EVT_WIFI_STATE, .val = st };
    app_post_evt(&e);
}

static void toast(const char *msg)
{
    app_evt_t e = { .id = EVT_TOAST };
    strlcpy(e.str, msg, sizeof(e.str));
    app_post_evt(&e);
}

/* ============================ eventos WiFi ================================ */
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            set_state(WIFI_CONNECTING);
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            if (s_state == WIFI_CONNECTING || s_state == WIFI_ONLINE) {
                if (s_retries++ < WIFI_MAX_RETRY) {
                    ESP_LOGW(TAG, "WiFi caiu/falhou (%d/%d) - tentando de novo",
                             s_retries, WIFI_MAX_RETRY);
                    esp_wifi_connect();
                    set_state(WIFI_CONNECTING);
                } else {
                    set_state(WIFI_FAILED);
                    toast("Sem WiFi: clima sera simulado.");
                }
            }
            break;
        case WIFI_EVENT_AP_STACONNECTED:
        case WIFI_EVENT_AP_STADISCONNECTED:
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retries = 0;
        set_state(WIFI_ONLINE);
        s_weather_req = true;
    }
}

/* ============================ portal web ================================== */
static const char PORTAL_HTML[] =
    "<!doctype html><html lang=pt><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Formigueiro</title></head>"
    "<body style='font-family:sans-serif;background:#241a12;color:#f0eadc;"
    "padding:28px;margin:0'>"
    "<h2 style='color:#ffc430'>&#128028; Formigueiro</h2>"
    "<p>Configure o WiFi do habitat. Depois de salvar, a placa conecta "
    "sozinha e baixa a hora e o clima da sua cidade.</p>"
    "<form method=POST action=/cfg style='display:grid;gap:12px;max-width:340px'>"
    "<input name=ssid required maxlength=31 placeholder='Nome da rede (SSID)' "
    "style='padding:12px;border-radius:8px;border:1px solid #604a36;background:#3a2c22;color:#f0eadc'>"
    "<input name=pass type=password maxlength=63 placeholder='Senha da rede' "
    "style='padding:12px;border-radius:8px;border:1px solid #604a36;background:#3a2c22;color:#f0eadc'>"
    "<button style='padding:12px;border-radius:8px;border:0;background:#ffc430;"
    "color:#241a12;font-weight:bold'>Salvar e conectar</button></form>"
    "<p id=s style='color:#8ca' ></p>"
    "<script>fetch('/status').then(r=>r.json()).then(j=>{"
    "document.getElementById('s').innerText='estado: '+j.state}).catch(()=>{})</script>"
    "</body></html>";

static char *url_decode(char *dst, const char *src, int len, size_t cap)
{
    size_t o = 0;
    for (int i = 0; i < len && o + 1 < cap; i++) {
        char ch = src[i];
        if (ch == '+') {
            dst[o++] = ' ';
        } else if (ch == '%' && i + 2 < len) {
            char hex[3] = { src[i + 1], src[i + 2], 0 };
            dst[o++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else {
            dst[o++] = ch;
        }
    }
    dst[o] = 0;
    return dst;
}

static esp_err_t handler_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, PORTAL_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t handler_status(httpd_req_t *req)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"state\":\"%s\",\"ssid\":\"%s\"}",
             s_state == WIFI_AP_PORTAL ? "portal" : "aguardando",
             appcfg_get()->wifi_ssid);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t handler_cfg(httpd_req_t *req)
{
    char body[256];
    int total = req->content_len;
    if (total <= 0 || total > (int)sizeof(body) - 1) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body");
        return ESP_FAIL;
    }
    int got = 0;
    while (got < total) {
        int r = httpd_req_recv(req, body + got, total - got);
        if (r <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
            return ESP_FAIL;
        }
        got += r;
    }
    body[got] = 0;

    char ssid[33] = { 0 }, pass[65] = { 0 };
    char *s = strstr(body, "ssid=");
    char *p = strstr(body, "pass=");
    if (!s) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid?");
        return ESP_FAIL;
    }
    s += 5;
    char *end = strchr(s, '&');
    url_decode(ssid, s, end ? (int)(end - s) : (int)strlen(s), sizeof(ssid));
    if (p) {
        p += 5;
        char *end2 = strchr(p, '&');
        url_decode(pass, p, end2 ? (int)(end2 - p) : (int)strlen(p), sizeof(pass));
    }
    if (!ssid[0]) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid vazio");
        return ESP_FAIL;
    }

    strlcpy(s_new_ssid, ssid, sizeof(s_new_ssid));
    strlcpy(s_new_pass, pass, sizeof(s_new_pass));
    s_apply_cred = true;
    ESP_LOGI(TAG, "credenciais recebidas do portal: '%s'", s_new_ssid);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req, "<html><body style='font-family:sans-serif;"
                            "background:#241a12;color:#8ca;padding:28px'>"
                            "<h2>Salvo!</h2><p>A placa vai conectar em "
                            "<b></b> e voltar ao jogo. Ja pode fechar esta pagina.</p>"
                            "</body></html>");
    return ESP_OK;
}

/* ------- captive portal: DNS responde TUDO com 192.168.4.1 -------
     * Assim o celular/PC abre o portal sozinho ao conectar no AP. */
static volatile bool s_dns_on = false;

static void dns_captive_task(void *arg)
{
    (void)arg;
    int fd = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) { vTaskDelete(NULL); return; }
    struct sockaddr_in sa = { 0 };
    sa.sin_family = AF_INET;
    sa.sin_port = htons(53);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (lwip_bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        lwip_close(fd); vTaskDelete(NULL); return;
    }
    struct timeval tv = { .tv_sec = 0, .tv_usec = 200000 };
    lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t q[300], r[340];
    while (s_dns_on) {
        int n = lwip_recv(fd, q, sizeof(q), 0);
        if (n < 12 || n > (int)sizeof(q) - 20) continue;
        memcpy(r, q, (size_t)n);
        r[2] |= 0x80;                    /* QR: resposta */
        r[3] |= 0x80;                    /* RA */
        r[6] = 0; r[7] = 1;              /* ANCOUNT = 1 */
        int o = n;
        r[o++] = 0xC0; r[o++] = 0x0C;    /* nome = ponteiro p/ pergunta */
        r[o++] = 0x00; r[o++] = 0x01;    /* tipo A */
        r[o++] = 0x00; r[o++] = 0x01;    /* classe IN */
        r[o++] = 0x00; r[o++] = 0x00; r[o++] = 0x00; r[o++] = 60;  /* TTL */
        r[o++] = 0x00; r[o++] = 0x04;    /* rdlength */
        r[o++] = 192; r[o++] = 168; r[o++] = 4; r[o++] = 1;
        lwip_send(fd, r, o, 0);
    }
    lwip_close(fd);
    vTaskDelete(NULL);
}

static void portal_start(void)
{
    if (s_httpd) return;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 4;
    cfg.stack_size = 4096;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd falhou");
        return;
    }
    httpd_uri_t u1 = { .uri = "/", .method = HTTP_GET, .handler = handler_root };
    httpd_uri_t u2 = { .uri = "/status", .method = HTTP_GET, .handler = handler_status };
    httpd_uri_t u3 = { .uri = "/cfg", .method = HTTP_POST, .handler = handler_cfg };
    httpd_register_uri_handler(s_httpd, &u1);
    httpd_register_uri_handler(s_httpd, &u2);
    httpd_register_uri_handler(s_httpd, &u3);
    ESP_LOGI(TAG, "portal web ativo em http://192.168.4.1");
    if (!s_dns_on) {
        s_dns_on = true;
        xTaskCreate(dns_captive_task, "cpdns", 2560, NULL, 3, NULL);
    }
}

static void portal_stop(void)
{
    s_dns_on = false;
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
}

static void station_start(void);   /* fwd */

static void provision_off(void)
{
    portal_stop();
    esp_wifi_stop();
    if (s_ap_netif) {
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
    }
    if (appcfg_get()->wifi_ssid[0]) station_start();
    else set_state(WIFI_OFF);
    toast("Portal WiFi fechado.");
}

void net_start_provision(void) { s_prov_req = true; }

static void provision_on(void)
{
    esp_wifi_stop();
    if (!s_ap_netif) s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.ap.ssid, s_ap_ssid, sizeof(wc.ap.ssid));
    wc.ap.ssid_len = (uint8_t)strlen(s_ap_ssid);
    strlcpy((char *)wc.ap.password, WIFI_AP_PASS, sizeof(wc.ap.password));
    wc.ap.channel = 6;
    wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wc.ap.max_connection = 2;
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wc);
    esp_wifi_start();
    portal_start();
    set_state(WIFI_AP_PORTAL);
    {
        char t[96];
        snprintf(t, sizeof(t), "Portal: %s / senha %s / 192.168.4.1",
                 s_ap_ssid, WIFI_AP_PASS);
        toast(t);
    }
}

static void station_start(void)
{
    app_config_t *cfg = appcfg_get();
    if (!cfg->wifi_ssid[0]) return;
    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, cfg->wifi_ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, cfg->wifi_pass, sizeof(wc.sta.password));
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    s_retries = 0;
    esp_wifi_start();
    set_state(WIFI_CONNECTING);
}

/* ============================ HTTP GET ==================================== */
static char *http_get(const char *url, size_t cap)
{
    char *buf = malloc(cap);
    if (!buf) return NULL;
    size_t got = 0;

    esp_http_client_config_t cc = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 9000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 1024,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&cc);
    if (!cl) { free(buf); return NULL; }

    esp_err_t err = esp_http_client_open(cl, 0);
    int status = 0;
    if (err == ESP_OK) {
        err = esp_http_client_fetch_headers(cl) < 0 ? ESP_FAIL : ESP_OK;
        status = esp_http_client_get_status_code(cl);
    }
    if (err == ESP_OK && status == 200) {
        int n;
        while (got + 1 < cap &&
               (n = esp_http_client_read(cl, buf + got, (int)(cap - 1 - got))) > 0) {
            got += (size_t)n;
        }
    } else if (err == ESP_OK) {
        err = ESP_FAIL;
        ESP_LOGW(TAG, "HTTP %d para %s", status, url);
    }
    esp_http_client_cleanup(cl);

    if (err != ESP_OK || got == 0) {
        free(buf);
        return NULL;
    }
    buf[got] = 0;
    return buf;
}

/* tenta https e, se o heap não aguentar TLS, cai para http puro */
static char *http_get_fallback(const char *url, size_t cap)
{
    char *r = http_get(url, cap);
    if (r || strncmp(url, "https://", 8) != 0) return r;

    char *plain = malloc(strlen(url) + 1);
    if (!plain) return NULL;
    snprintf(plain, strlen(url) + 1, "http://%s", url + 8);
    ESP_LOGW(TAG, "TLS falhou - tentando HTTP puro");
    r = http_get(plain, cap);
    free(plain);
    return r;
}

/* percent-encode mínimo p/ URL (espaço e bytes UTF-8) */
static void url_encode(char *dst, size_t cap, const char *src)
{
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p && o + 4 < cap; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.') {
            dst[o++] = (char)*p;
        } else if (*p == ' ') {
            dst[o++] = '+';
        } else {
            o += (size_t)snprintf(dst + o, cap - o, "%%%02X", *p);
        }
    }
    dst[o] = 0;
}

/* ============================ SNTP ======================================== */
static void sntp_cb(struct timeval *tv)
{
    time_t now = tv->tv_sec;
    struct tm tm;
    localtime_r(&now, &tm);
    ESP_LOGI(TAG, "NTP ok: %04d-%02d-%02d %02d:%02d", tm.tm_year + 1900,
             tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
    app_evt_t e = { .id = EVT_TIME_SYNC, .val = (int32_t)(now - 1700000000L) };
    app_post_evt(&e);
}

static void sntp_start(void)
{
    if (s_sntp_done) return;
    s_sntp_done = true;
    setenv("TZ", appcfg_get()->tz[0] ? appcfg_get()->tz : "<-03>3", 1);
    tzset();
    esp_sntp_set_sync_interval(30 * 60 * 1000);
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER);
    sntp_set_time_sync_notification_cb(sntp_cb);
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP iniciado (TZ=%s)", appcfg_get()->tz);
}

/* ============================ consultas =================================== */
static void do_geocode(void)
{
    char enc[48], url[256];
    url_encode(enc, sizeof(enc), s_geo_req);
    snprintf(url, sizeof(url), GEO_URL_FMT, enc);
    char *json = http_get_fallback(url, 2048);
    if (!json) {
        toast("Geocoding falhou (sem internet?).");
        return;
    }
    int16_t la, lo;
    if (weather_parse_geocode(json, &la, &lo)) {
        app_config_t *cfg = appcfg_get();
        cfg->lat_x100 = la;
        cfg->lon_x100 = lo;
        cfg->geo_ok = true;
        strlcpy(cfg->city, s_geo_req, sizeof(cfg->city));
        appcfg_save();
        s_weather_req = true;
        toast("Cidade atualizada: clima real ativo.");
    } else {
        toast("Cidade nao encontrada.");
    }
    free(json);
}

static void do_weather(void)
{
    app_config_t *cfg = appcfg_get();
    if (!cfg->weather_enabled || !cfg->geo_ok) return;
    char url[512];
    snprintf(url, sizeof(url), WEATHER_URL_FMT, cfg->lat_x100 / 100.0,
             cfg->lon_x100 / 100.0);
    char *json = http_get_fallback(url, 4096);
    if (!json) {
        ESP_LOGW(TAG, "clima indisponivel — mantendo simulacao");
        return;
    }
    uint8_t wx, hum, pp;
    int16_t temp;
    if (weather_parse_forecast(json, &wx, &temp, &hum, &pp)) {
        app_evt_t e = { .id = EVT_WEATHER, .val = wx, .val2 = temp,
                        .b1 = hum, .b2 = pp };
        strlcpy(e.str, cfg->city, sizeof(e.str));
        app_post_evt(&e);
        ESP_LOGI(TAG, "clima real: %s, %d.%d C, umid %u%%",
                 weather_name(wx), temp / 10, abs(temp % 10), hum);
    }
    free(json);
}

/* ============================ tarefa ====================================== */
static bool s_boot_done = false;

static void net_task(void *arg)
{
    int64_t last_ms = 0;
    while (true) {
        int64_t now = now_ms();

        if (!s_boot_done) {
            s_boot_done = true;
            if (!appcfg_get()->wifi_ssid[0]) {
                ESP_LOGI(TAG, "sem WiFi configurado - abrindo portal automaticamente");
                provision_on();
            } else {
                station_start();
            }
        }
        if (s_prov_req) {
            s_prov_req = false;
            if (s_state == WIFI_ONLINE)      toast("WiFi ja esta conectado.");
            else if (s_state == WIFI_AP_PORTAL) provision_off();
            else                             provision_on();
        }
        if (s_apply_cred) {
            s_apply_cred = false;
            app_config_t *cfg = appcfg_get();
            strlcpy(cfg->wifi_ssid, s_new_ssid, sizeof(cfg->wifi_ssid));
            strlcpy(cfg->wifi_pass, s_new_pass, sizeof(cfg->wifi_pass));
            cfg->wifi_ok = true;
            appcfg_save();
            portal_stop();
            esp_wifi_stop();
            if (s_ap_netif) {
                esp_netif_destroy(s_ap_netif);
                s_ap_netif = NULL;
            }
            station_start();
        }

        if (s_state == WIFI_ONLINE) {
            if (!s_sntp_done) sntp_start();
            if (s_geo_req[0]) {
                char g[25];
                strlcpy(g, s_geo_req, sizeof(g));
                s_geo_req[0] = 0;
                do_geocode();
            }
            if (s_weather_req || now - s_last_weather_ms > WEATHER_REFRESH_MIN * 60000) {
                s_weather_req = false;
                s_last_weather_ms = now;
                do_weather();
            }
        } else if (s_state == WIFI_FAILED && appcfg_get()->wifi_ssid[0] &&
                   now - last_ms > 60000) {
            /* nova tentativa periódica */
            last_ms = now;
            s_retries = 0;
            esp_wifi_connect();
            set_state(WIFI_CONNECTING);
        }

        vTaskDelay(pdMS_TO_TICKS(400));
    }
}

/* ============================ init ======================================== */
void net_init(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s-%02X%02X",
             WIFI_AP_SSID_PREFIX, mac[4], mac[5]);

    wifi_init_config_t wc = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&wc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_init falhou: %s", esp_err_to_name(err));
        set_state(WIFI_FAILED);
        return;
    }
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        wifi_event_handler, NULL, NULL);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

    esp_netif_create_default_wifi_sta();

    xTaskCreatePinnedToCore(net_task, "net", NET_TASK_STACK, NULL,
                            PRIO_NET, NULL, CORE_NET);

    if (appcfg_get()->wifi_ssid[0]) {
        station_start();
    } else {
        set_state(WIFI_OFF);
        ESP_LOGI(TAG, "sem WiFi configurado — use Configurações > WIFI");
    }
}
