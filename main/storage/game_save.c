/*
 * game_save.c — Save do jogo em flash bruta com pingue-pongue de setores.
 */
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_crc.h"
#include "esp_flash.h"

#include "game_save.h"

static const char *TAG = "save";

#define SECTOR_SZ 4096

static const esp_partition_t *s_part;
static uint32_t s_base;
static uint32_t s_seq;
static int      s_last_sector = -1;
static uint8_t *s_buf;          /* payload do save carregado */
static size_t   s_len;

static uint32_t crc_of(const save_hdr_t *h, const uint8_t *payload, size_t len)
{
    uint32_t c = esp_crc32_le(0, (const uint8_t *)h,
                              sizeof(save_hdr_t) - sizeof(uint32_t));
    return esp_crc32_le(c, payload, len);
}

/* Lê e valida um setor; devolve true se estiver íntegro. */
static bool read_sector(int idx, save_hdr_t *hdr, uint8_t *payload, size_t cap,
                        size_t *out_len)
{
    uint32_t off = s_base + (uint32_t)idx * SECTOR_SZ;
    if (esp_flash_read(NULL, hdr, off, sizeof(*hdr)) != ESP_OK) {
        return false;
    }
    if (hdr->magic != SAVE_MAGIC || hdr->version != SAVE_VERSION ||
        hdr->size == 0 || hdr->size > cap) {
        return false;
    }
    if (esp_flash_read(NULL, payload, off + sizeof(*hdr), hdr->size) != ESP_OK) {
        return false;
    }
    if (hdr->crc != crc_of(hdr, payload, hdr->size)) {
        ESP_LOGW(TAG, "setor %d com CRC invalido", idx);
        return false;
    }
    *out_len = hdr->size;
    return true;
}

esp_err_t game_save_init(void)
{
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                      ESP_PARTITION_SUBTYPE_ANY, "save");
    if (!s_part) {
        ESP_LOGE(TAG, "particao 'save' nao encontrada (ver partitions.csv)");
        return ESP_ERR_NOT_FOUND;
    }
    s_base = s_part->address;
    if (s_part->size < 2 * SECTOR_SZ) {
        ESP_LOGE(TAG, "particao 'save' pequena demais (%u B)", (unsigned)s_part->size);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "particao save: addr=0x%08lx tam=%lu B",
             (unsigned long)s_base, (unsigned long)s_part->size);

    static save_hdr_t h0, h1;
    s_buf = malloc(SECTOR_SZ - sizeof(save_hdr_t));
    if (!s_buf) {
        return ESP_ERR_NO_MEM;
    }
    uint8_t *tmp = malloc(SECTOR_SZ - sizeof(save_hdr_t));
    if (!tmp) {
        free(s_buf);
        s_buf = NULL;
        return ESP_ERR_NO_MEM;
    }

    size_t l0 = 0, l1 = 0;
    bool ok0 = read_sector(0, &h0, s_buf, SECTOR_SZ - sizeof(save_hdr_t), &l0);
    bool ok1 = read_sector(1, &h1, tmp,   SECTOR_SZ - sizeof(save_hdr_t), &l1);

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    if (ok0 && ok1) {
        /* ambos válidos: fica com o de seq maior */
        int32_t d = (int32_t)(h0.seq - h1.seq);
        if (d >= 0) {
            s_seq = h0.seq; s_len = l0; s_last_sector = 0; ret = ESP_OK;
        } else {
            memcpy(s_buf, tmp, l1);
            s_seq = h1.seq; s_len = l1; s_last_sector = 1; ret = ESP_OK;
        }
    } else if (ok0) {
        s_seq = h0.seq; s_len = l0; s_last_sector = 0; ret = ESP_OK;
    } else if (ok1) {
        memcpy(s_buf, tmp, l1);
        s_seq = h1.seq; s_len = l1; s_last_sector = 1; ret = ESP_OK;
    }
    free(tmp);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "save carregado: seq=%lu bytes=%u (setor %d)",
                 (unsigned long)s_seq, (unsigned)s_len, s_last_sector);
    } else {
        ESP_LOGI(TAG, "nenhum save valido - colonia sera criada do zero");
    }
    return ret;
}

const void *game_save_data(size_t *len)
{
    if (len) *len = s_len;
    return s_buf;
}

uint32_t game_save_seq(void)
{
    return s_seq;
}

esp_err_t game_save_write(const void *data, size_t len)
{
    if (!s_buf || len == 0 || len > SECTOR_SZ - sizeof(save_hdr_t)) {
        return ESP_ERR_INVALID_SIZE;
    }
    int sector = (s_last_sector == 0) ? 1 : 0;
    uint32_t off = s_base + (uint32_t)sector * SECTOR_SZ;

    esp_err_t err = esp_partition_erase_range(s_part, (uint32_t)sector * SECTOR_SZ,
                                              SECTOR_SZ);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "erase setor %d falhou: %s", sector, esp_err_to_name(err));
        return err;
    }

    save_hdr_t h = {
        .magic = SAVE_MAGIC,
        .version = SAVE_VERSION,
        .size = (uint16_t)len,
        .seq = s_seq + 1,
    };
    h.crc = crc_of(&h, (const uint8_t *)data, len);

    err = esp_flash_write(NULL, &h, off, sizeof(h));
    if (err == ESP_OK) {
        err = esp_flash_write(NULL, data, off + sizeof(h), len);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gravacao falhou: %s", esp_err_to_name(err));
        return err;
    }

    /* relê para confirmar */
    save_hdr_t chk;
    if (!read_sector(sector, &chk, s_buf, SECTOR_SZ - sizeof(save_hdr_t), &s_len)) {
        ESP_LOGE(TAG, "verificacao do setor %d falhou - save anterior mantido", sector);
        return ESP_FAIL;
    }
    memcpy(s_buf, data, len);
    s_len = len;
    s_seq = h.seq;
    s_last_sector = sector;
    return ESP_OK;
}

esp_err_t game_save_erase(void)
{
    if (!s_part) return ESP_ERR_INVALID_STATE;
    esp_err_t e1 = esp_partition_erase_range(s_part, 0, SECTOR_SZ);
    esp_err_t e2 = esp_partition_erase_range(s_part, SECTOR_SZ, SECTOR_SZ);
    s_seq = 0;
    s_len = 0;
    s_last_sector = -1;
    ESP_LOGI(TAG, "save apagado (novo jogo)");
    return (e1 != ESP_OK) ? e1 : e2;
}
