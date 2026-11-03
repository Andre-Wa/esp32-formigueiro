/* Gerado automaticamente por tools/gen_font_ext.py - NAO EDITAR */
#pragma once
#include <stdint.h>

#define FONTEXT_FIRST 0x80
#define FONTEXT_LAST  0xFF
#define FONTEXT_NUM   (FONTEXT_LAST - FONTEXT_FIRST + 1)
#define FONTEXT_W     8
#define FONTEXT_H     16

/* glifos próprios (acentos isolados, °, ·, ©, ± ...) — bit7 = pixel esquerdo */
extern const uint8_t fontext_glyphs[FONTEXT_NUM][FONTEXT_H];
/* letra ASCII base de cada codepoint Latin-1 (0 = só o glifo próprio) */
extern const uint8_t fontext_alias[FONTEXT_NUM];
/* acento combinante: índice em fontext_glyphs (0 = nenhum) */
extern const uint8_t fontext_comb[FONTEXT_NUM];
