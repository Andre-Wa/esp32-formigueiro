/*
 * game_save.h — Persistência do estado do jogo em flash bruta (partição "save").
 *
 * Estratégia: dois setores de 4 KB usados em pingue-pongue. Cada gravação vai
 * para o setor oposto ao último válido, com cabeçalho {magic, versão, seq,
 * tamanho, crc32}. Assim uma queda de energia no meio da gravação nunca
 * perde o save anterior, e o desgaste é dividido entre os dois setores.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define SAVE_MAGIC   0x4D524F46u   /* "FORM" */
#define SAVE_VERSION 1

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t seq;
    uint32_t crc;
} save_hdr_t;

/* Abre a partição e carrega o save mais recente válido.
 * Retorna ESP_ERR_NOT_FOUND se não houver save (primeiro boot). */
esp_err_t game_save_init(void);

/* Lê o payload carregado (NULL se não houver save). */
const void *game_save_data(size_t *len);

/* Grava o payload no setor livre (gravação real na flash). */
esp_err_t game_save_write(const void *data, size_t len);

/* Apaga os dois setores (novo jogo). */
esp_err_t game_save_erase(void);

uint32_t game_save_seq(void);
