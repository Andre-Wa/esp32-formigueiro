#!/usr/bin/env python3
"""
gen_font_ext.py — gera main/display/fontext.{c,h}

A font8x16 herdada do cyd-player cobre apenas ASCII 32..126. Este gerador cria
o suporte Latin-1 (0x80..0xFF) usado pelo texto em português do jogo:

  * fontext_glyphs[]  — glifos próprios (acentos isolados, °, ·, ¸, º...)
  * fontext_alias[]   — para cada codepoint Latin-1, a letra ASCII base
                        (0 = usar o glifo próprio / nada)
  * fontext_comb[]    — acento combinante a sobrepor à letra base

O gfx_ext (gtext) resolve: se alias != 0 desenha a letra ASCII da font8x16 e,
por cima, o acento combinante; senão desenha o glifo próprio.
"""
import os

FIRST, LAST = 0x80, 0xFF
H = 16
BLANK = [0x00] * H


def mk(rows):
    g = BLANK[:]
    for y, v in rows.items():
        g[y] = v
    return g


# ---- glifos próprios ----
ACCUTE = mk({2: 0x0C, 3: 0x18})          # ´
GRAVE = mk({2: 0x30, 3: 0x18})           # `
CIRCUM = mk({2: 0x24, 3: 0x18})          # ^
TILDE = mk({2: 0x5A})                    # ~
DIER = mk({2: 0x28})                     # ¨
CEDILLA = mk({12: 0x18, 13: 0x0C, 14: 0x18})
RING = mk({1: 0x18, 2: 0x24, 3: 0x18})   # ° (anel do å / grau)

GLYPHS = {
    0x00: BLANK,                          # slot vazio
    0x01: ACCUTE,
    0x02: GRAVE,
    0x03: CIRCUM,
    0x04: TILDE,
    0x05: DIER,
    0x06: CEDILLA,
    0x07: RING,
    0x80: mk({8: 0x3C, 9: 0x24, 10: 0x24, 11: 0x3C}),   # € aproximado (EU)
    0x82: mk({5: 0x3E, 6: 0x08, 7: 0x3E, 8: 0x08, 9: 0x3E}),  # ‚ -> traços
    0x85: mk({8: 0x18, 9: 0x18}),                          # … -> ponto
    0x91: mk({3: 0x0C, 4: 0x18}),                          # ‘
    0x92: mk({3: 0x18, 4: 0x0C}),                          # ’
    0x93: mk({3: 0x66, 4: 0x66}),                          # “
    0x94: mk({3: 0x66, 4: 0x66}),                          # ”
    0x95: mk({7: 0x7E, 8: 0x7E}),                          # •
    0x96: mk({8: 0x7E}),                                   # –
    0x97: mk({8: 0x7E}),                                   # —
    0xA0: BLANK,                                           # NBSP
    0xA1: mk({4: 0x18, 5: 0x18, 6: 0x18, 7: 0x18, 8: 0x18, 11: 0x18, 12: 0x18}),  # ¡
    0xA6: mk({4: 0x18, 5: 0x18, 6: 0x18, 9: 0x18, 10: 0x18, 11: 0x18}),           # ¦
    0xA7: mk({4: 0x3C, 5: 0x42, 6: 0x18, 7: 0x24, 8: 0x42, 9: 0x3C}),             # §
    0xA9: mk({3: 0x3C, 4: 0x42, 5: 0x99, 6: 0xA1, 7: 0xA1, 8: 0x99, 9: 0x42, 10: 0x3C}),  # ©
    0xAB: mk({5: 0x24, 6: 0x48, 7: 0x90, 6 + 1: 0x48, 9: 0x24}),                  # «
    0xAC: mk({6: 0x7E, 7: 0x06, 8: 0x06}),                                        # ¬
    0xAD: mk({8: 0x3C}),                                                          # soft hyphen
    0xAE: mk({3: 0x3C, 4: 0x42, 5: 0xB9, 6: 0xB5, 7: 0xB9, 8: 0xB5, 9: 0x42, 10: 0x3C}),  # ®
    0xB0: mk({3: 0x18, 4: 0x24, 5: 0x24, 6: 0x18}),                               # °
    0xB1: mk({4: 0x18, 5: 0x18, 6: 0x7E, 7: 0x18, 8: 0x18, 11: 0x7E}),            # ±
    0xB5: mk({6: 0x66, 7: 0x66, 8: 0x66, 9: 0x66, 10: 0x7C, 11: 0x60, 12: 0xE0}), # µ
    0xB6: mk({4: 0x3E, 5: 0x56, 6: 0x56, 7: 0x36, 8: 0x16, 9: 0x16, 10: 0x16}),   # ¶
    0xB7: mk({8: 0x18, 9: 0x18}),                                                 # ·
    0xBF: mk({4: 0x18, 5: 0x18, 6: 0x00, 7: 0x1C, 8: 0x22, 9: 0x30, 10: 0x18, 11: 0x0C, 12: 0x22, 13: 0x3C}),  # ¿
    0xD7: mk({5: 0x42, 6: 0x24, 7: 0x18, 8: 0x24, 9: 0x42}),                      # ×
    0xF7: mk({5: 0x18, 6: 0x18, 8: 0x7E, 10: 0x18, 11: 0x18}),                    # ÷
}

# ---- letra base + acento combinante ----
NONE, ACU, GRA, CIR, TIL, DIE, CED, RNG = 0, 1, 2, 3, 4, 5, 6, 7
MAP = {
    0xC0: ('A', GRA), 0xC1: ('A', ACU), 0xC2: ('A', CIR), 0xC3: ('A', TIL),
    0xC4: ('A', DIE), 0xC5: ('A', RNG),
    0xC6: ('A', NONE),                     # Æ -> A
    0xC7: ('C', CED),
    0xC8: ('E', GRA), 0xC9: ('E', ACU), 0xCA: ('E', CIR), 0xCB: ('E', DIE),
    0xCC: ('I', GRA), 0xCD: ('I', ACU), 0xCE: ('I', CIR), 0xCF: ('I', DIE),
    0xD0: ('D', NONE),                     # Ð
    0xD1: ('N', TIL),
    0xD2: ('O', GRA), 0xD3: ('O', ACU), 0xD4: ('O', CIR), 0xD5: ('O', TIL),
    0xD6: ('O', DIE),
    0xD8: ('O', NONE),                     # Ø
    0xD9: ('U', GRA), 0xDA: ('U', ACU), 0xDB: ('U', CIR), 0xDC: ('U', DIE),
    0xDD: ('Y', ACU),
    0xDE: ('P', NONE),                     # Þ
    0xDF: ('B', NONE),                     # ß
    0xE0: ('a', GRA), 0xE1: ('a', ACU), 0xE2: ('a', CIR), 0xE3: ('a', TIL),
    0xE4: ('a', DIE), 0xE5: ('a', RNG),
    0xE6: ('a', NONE),
    0xE7: ('c', CED),
    0xE8: ('e', GRA), 0xE9: ('e', ACU), 0xEA: ('e', CIR), 0xEB: ('e', DIE),
    0xEC: ('i', GRA), 0xED: ('i', ACU), 0xEE: ('i', CIR), 0xEF: ('i', DIE),
    0xF0: ('o', NONE),
    0xF1: ('n', TIL),
    0xF2: ('o', GRA), 0xF3: ('o', ACU), 0xF4: ('o', CIR), 0xF5: ('o', TIL),
    0xF6: ('o', DIE),
    0xF8: ('o', NONE),
    0xF9: ('u', GRA), 0xFA: ('u', ACU), 0xFB: ('u', CIR), 0xFC: ('u', DIE),
    0xFD: ('y', ACU),
    0xFE: ('p', NONE),
    0xFF: ('y', DIE),
    # pontuação/símbolos com equivalente ASCII direto
    0x83: ('f', NONE), 0x88: ('S', NONE), 0x89: ('S', NONE),
    0x8A: ('S', NONE), 0x8C: ('O', NONE), 0x8D: ('Z', NONE),
    0x8E: ('Z', NONE), 0x9A: ('s', NONE), 0x9C: ('o', NONE),
    0x9D: ('z', NONE), 0x9E: ('z', NONE),
    0xA2: ('c', NONE), 0xA3: ('L', NONE), 0xA4: ('o', NONE),
    0xA5: ('Y', NONE), 0xA8: ('.', DIE), 0xAA: ('o', ACU), 0xB2: ('2', NONE),
    0xB3: ('3', NONE), 0xB4: ('.', ACU), 0xB8: ('.', CED), 0xB9: ('1', NONE),
    0xBA: ('o', GRA), 0xBC: ('4', NONE), 0xBD: ('2', NONE), 0xBE: ('4', NONE),
    0x9F: ('Y', DIE),
}

out = []
out.append("/* Gerado automaticamente por tools/gen_font_ext.py - NAO EDITAR */")
out.append('#include "fontext.h"')
out.append("")
out.append("const uint8_t fontext_glyphs[FONTEXT_NUM][FONTEXT_H] = {")
for cp in range(FIRST, LAST + 1):
    g = GLYPHS.get(cp, BLANK)
    out.append("    { /* 0x%02X */ %s }," % (cp, ", ".join("0x%02X" % b for b in g)))
out.append("};")
out.append("")
out.append("/* letra ASCII base (0 = usar o glifo próprio acima) */")
out.append("const uint8_t fontext_alias[FONTEXT_NUM] = {")
out.append("    " + ", ".join("0x%02X" % (ord(MAP[cp][0]) if cp in MAP else 0)
                               for cp in range(FIRST, LAST + 1)))
out.append("};")
out.append("")
out.append("/* acento combinante: índice em fontext_glyphs (0 = nenhum) */")
out.append("const uint8_t fontext_comb[FONTEXT_NUM] = {")
out.append("    " + ", ".join("0x%02X" % (MAP[cp][1] if cp in MAP else 0)
                               for cp in range(FIRST, LAST + 1)))
out.append("};")
out.append("")

base = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(base, "../main/display/fontext.c"), "w") as f:
    f.write("\n".join(out) + "\n")

hdr = """/* Gerado automaticamente por tools/gen_font_ext.py - NAO EDITAR */
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
"""
with open(os.path.join(base, "../main/display/fontext.h"), "w") as f:
    f.write(hdr)

n_alias = sum(1 for cp in range(FIRST, LAST + 1) if cp in MAP)
n_own = sum(1 for cp in range(FIRST, LAST + 1) if cp in GLYPHS)
print("OK: %d codepoints Latin-1 (%d com letra base, %d glifos proprios)"
      % (LAST - FIRST + 1, n_alias, n_own))
