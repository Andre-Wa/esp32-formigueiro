#!/usr/bin/env python3
"""
gen_sprites.py — fonte única da pixel art do Formigueiro.

  1. desenha todos os sprites (alguns proceduralmente, outros como arte ASCII);
  2. emite main/game/sprites.c (paletas + pixels em flash);
  3. emite docs/sprites.bmp  — folha de contato ampliada 4x para conferência;
  4. emite docs/scene_colony.bmp e docs/scene_outside.bmp — prévia das cenas.

Uso:  python3 tools/gen_sprites.py
Depois: idf.py build  (o sprites.c gerado já está versionado).
"""
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_C = os.path.join(HERE, "..", "main", "game", "sprites.c")
DOCS = os.path.join(HERE, "..", "docs")

# ============================== paleta ======================================
# índice -> (r,g,b).  O índice 0 é SEMPRE transparente.
PAL = {
    0:  (0, 0, 0),        # transparente
    1:  (42, 26, 20),     # corpo da formiga (sombra)
    2:  (74, 46, 30),     # corpo meio-tom
    3:  (122, 82, 50),    # corpo luz
    4:  (240, 236, 220),  # olho/brilho
    5:  (62, 34, 26),     # corpo da rainha
    6:  (112, 62, 40),    # rainha meio-tom
    7:  (176, 112, 66),   # rainha luz
    8:  (255, 208, 64),   # dourado (coroa, realce)
    9:  (255, 96, 128),   # coração
    10: (76, 148, 58),    # folha/grama escura
    11: (122, 196, 78),   # folha clara
    12: (196, 60, 72),    # fruta vermelha
    13: (240, 120, 140),  # fruta luz / pétala rosa
    14: (255, 232, 120),  # amarelo (flor, sol)
    15: (232, 176, 92),   # semente / trigo
    16: (126, 86, 52),    # madeira / graveto
    17: (180, 132, 88),   # madeira clara
    18: (222, 214, 196),  # ovo / pedra clara
    19: (168, 160, 148),  # pedra
    20: (246, 240, 222),  # branco ovo
    21: (250, 232, 190),  # larva
    22: (200, 160, 120),  # pupa
    23: (40, 34, 44),     # predador (aranha)
    24: (96, 168, 232),   # água / céu
    25: (232, 232, 240),  # nuvem / brilho
    26: (255, 255, 255),  # branco puro
    27: (240, 168, 56),   # alerta
    28: (104, 200, 108),  # ok/verde UI
    29: (232, 88, 72),    # erro/vermelho UI
    30: (112, 176, 232),  # info/azul UI
    31: (164, 148, 128),  # cinza UI
    32: (240, 234, 220),  # texto UI
    33: (78, 50, 30),     # terra
    34: (116, 78, 46),    # terra clara
    35: (46, 28, 18),     # terra escura
    36: (44, 96, 38),     # grama escura
    37: (255, 196, 48),   # acento UI
}

CHARS = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()_+-=[]{}|;:,.<>?/~"


def char_to_idx(ch):
    """Mapeia o caractere da arte ASCII para o índice de paleta."""
    if ch == '.':
        return 0
    if ch.isdigit():
        return int(ch)
    # letras minúsculas: a=10, b=11, ... (10 + ord-97)
    if 'a' <= ch <= 'z':
        return 10 + (ord(ch) - ord('a'))
    if 'A' <= ch <= 'Z':
        return 36 + (ord(ch) - ord('A'))   # 36..  (A=36 grama escura etc.)
    return 0


# ============================== buffer de pixel =============================
class Buf:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.p = [[0] * w for _ in range(h)]

    def set(self, x, y, i):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.p[y][x] = i

    def get(self, x, y):
        if 0 <= x < self.w and 0 <= y < self.h:
            return self.p[y][x]
        return 0

    def rect(self, x, y, w, h, i):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.set(xx, yy, i)

    def hline(self, x, y, w, i):
        for xx in range(x, x + w):
            self.set(xx, y, i)

    def vline(self, x, y, h, i):
        for yy in range(y, y + h):
            self.set(x, yy, i)

    def line(self, x0, y0, x1, y1, i):
        dx, dy = abs(x1 - x0), abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx - dy
        while True:
            self.set(x0, y0, i)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 > -dy:
                err -= dy
                x0 += sx
            if e2 < dx:
                err += dx
                y0 += sy

    def ellipse(self, cx, cy, rx, ry, i, fill=True):
        for dy in range(-ry, ry + 1):
            for dx in range(-rx, rx + 1):
                v = (dx * dx) * ry * ry + (dy * dy) * rx * rx
                if v <= rx * rx * ry * ry:
                    if fill or v >= (rx - 1) * (rx - 1) * ry * ry * 0.7:
                        self.set(cx + dx, cy + dy, i)

    def circle(self, cx, cy, r, i, fill=True):
        self.ellipse(cx, cy, r, r, i, fill)

    def trim(self):
        """recorta bordas vazias"""
        xs = [x for y in range(self.h) for x in range(self.w) if self.p[y][x]]
        ys = [y for y in range(self.h) for x in range(self.w) if self.p[y][x]]
        if not xs:
            return self
        x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
        nb = Buf(x1 - x0 + 1, y1 - y0 + 1)
        for y in range(nb.h):
            for x in range(nb.w):
                nb.p[y][x] = self.p[y0 + y][x0 + x]
        return nb

    def mirror(self):
        nb = Buf(self.w, self.h)
        for y in range(self.h):
            for x in range(self.w):
                nb.p[y][x] = self.p[y][self.w - 1 - x]
        return nb

    def overlay(self, other, ox, oy):
        for y in range(other.h):
            for x in range(other.w):
                v = other.get(x, y)
                if v:
                    self.set(ox + x, oy + y, v)

def art_to_buf(art, w=None, h=None):
    rows = [r for r in art.strip('\n').split('\n')]
    h = h or len(rows)
    w = w or max(len(r) for r in rows)
    b = Buf(w, h)
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            b.set(x, y, char_to_idx(ch))
    return b


# ============================== formigas ====================================
def make_ant(w, h, body, hi, light, leg, phase=0, big=False, stripes=False,
             eye=4):
    """Formiga em vista lateral virada para a ESQUERDA, segmentos nítidos:
    cabeça + tórax + cintura + gaster, seis pernas em zigue-zague."""
    b = Buf(w, h)
    gy = h - 5                       # linha do corpo
    if big:
        gx, grx, gry = w - 7, 6, 4   # gaster grande
    else:
        gx, grx, gry = w - 5, 4, 3
    # gaster
    b.ellipse(gx, gy, grx, gry, body)
    b.ellipse(gx - 1, gy - 1, grx - 2, gry - 1, hi)
    b.set(gx - grx + 2, gy - gry + 1, light)
    if stripes:
        for k in range(gy - gry + 2, gy + gry - 1, 2):
            b.hline(gx - grx + 1, k, grx * 2 - 2, light)
    # cintura fina
    wx = gx - grx - 1
    b.set(wx, gy, body)
    b.set(wx, gy - 1, body)
    # tórax
    tx = wx - (3 if big else 2)
    b.ellipse(tx, gy - 1, 2, 2, body)
    b.set(tx - 1, gy - 2, hi)
    b.set(tx, gy - 3, hi)
    # cabeça
    hx = tx - (4 if big else 3)
    r = 3 if big else 2
    b.circle(hx, gy - 1, r, body)
    b.set(hx - 1, gy - 2, hi)
    b.set(hx - r + 1, gy - 1, eye)          # olho
    b.set(hx - r + 1, gy, eye)
    # mandíbulas
    b.set(hx - r - 1, gy, body)
    b.set(hx - r - 1, gy + 1, leg)
    b.set(hx - r, gy + 1, body)
    # antenas curvas
    b.line(hx - 1, gy - r - 1, hx - 3, gy - r - 3, leg)
    b.set(hx - 4, gy - r - 3, leg)
    b.line(hx, gy - r - 1, hx - 1, gy - r - 4, leg)
    b.set(hx - 1, gy - r - 4, leg)
    # pernas (3 pares, zigue-zague; phase alterna)
    lx = [tx - 2, tx, tx + 2]
    for n, x in enumerate(lx):
        d = 1 if ((n + phase) % 2 == 0) else -1
        b.line(x, gy + 1, x + d, gy + 2, leg)
        b.line(x + d, gy + 2, x + d * 2, h - 1, leg)
    return b


def make_queen(phase=0):
    b = make_ant(26, 16, 5, 6, 7, 1, phase, big=True, stripes=True, eye=8)
    return b


def make_crown():
    return art_to_buf("""
.8.8.8.
8888888
.88888.
""")


# ============================== cenário/comida ==============================
SPRITES = {}


def add(name, buf):
    SPRITES[name] = buf


def make_leaf():
    b = Buf(13, 9)
    pts = """
....aaa......
..aabbbaa....
.aabbbbbaa...
.abbbbbbbba..
.aabbbbbbaa..
..aabbbaa....
....aaa......
"""
    return art_to_buf(pts)


def make_leaf_pile():
    b = Buf(18, 11)
    b.overlay(art_to_buf("""
....aaa.......
..aabbbbaa....
.abbbbbbbba...
.aabbbbbba....
"""), 0, 4)
    b.overlay(art_to_buf("""
.....aaa......
...aabbbbaa...
..abbbbbbbba..
"""), 5, 0)
    b.line(2, 8, 15, 8, 10)
    return b


def make_berry():
    b = Buf(11, 12)
    b.circle(4, 7, 4, 12)
    b.circle(3, 6, 2, 13)
    b.set(2, 5, 26)
    b.line(5, 3, 7, 0, 16)
    b.ellipse(8, 2, 2, 1, 10)
    return b


def make_bush():
    b = Buf(24, 20)
    b.rect(10, 14, 4, 6, 16)
    b.circle(8, 9, 6, 36)
    b.circle(15, 8, 6, 10)
    b.circle(11, 5, 5, 11)
    b.circle(6, 11, 3, 10)
    b.circle(17, 11, 3, 36)
    # frutinhas
    for (x, y) in [(6, 7), (13, 6), (17, 10), (9, 12), (15, 13)]:
        b.circle(x, y, 1, 12)
        b.set(x - 1, y - 1, 13)
    return b


def make_flower(petal=13, center=14):
    b = Buf(11, 16)
    b.vline(5, 7, 9, 10)
    b.line(5, 11, 2, 9, 11)
    b.line(5, 13, 8, 11, 11)
    for dx, dy in [(0, -3), (3, 0), (0, 3), (-3, 0), (2, -2), (-2, -2), (2, 2), (-2, 2)]:
        b.circle(5 + dx, 5 + dy, 2, petal)
    b.circle(5, 5, 2, center)
    b.set(5, 4, 26)
    return b


def make_seeds():
    b = Buf(14, 8)
    for (x, y) in [(2, 5), (5, 4), (8, 5), (11, 4), (4, 6), (7, 6), (10, 6)]:
        b.ellipse(x, y, 1, 1, 15)
        b.set(x, y - 1, 17)
    return b


def make_twig():
    b = Buf(17, 8)
    b.line(1, 6, 15, 1, 16)
    b.line(1, 7, 15, 2, 17)
    b.line(6, 4, 8, 0, 16)
    b.line(11, 3, 13, 5, 16)
    return b


def make_mushroom():
    b = Buf(12, 11)
    b.ellipse(6, 4, 5, 3, 12)
    b.ellipse(6, 3, 4, 2, 13)
    b.set(4, 2, 26)
    b.set(7, 3, 26)
    b.rect(5, 6, 3, 5, 18)
    b.rect(5, 6, 1, 5, 20)
    return b


def make_egg():
    b = Buf(6, 8)
    b.ellipse(3, 4, 2, 3, 20)
    b.set(2, 2, 26)
    b.set(3, 6, 18)
    return b


def make_larva():
    b = Buf(11, 7)
    for i, x in enumerate(range(1, 10, 2)):
        b.circle(x, 3 + (i % 2), 2, 21)
    b.circle(1, 3, 2, 22)
    b.set(0, 2, 1)
    return b


def make_pupa():
    b = Buf(8, 11)
    b.ellipse(4, 5, 3, 5, 22)
    b.ellipse(4, 4, 2, 3, 17)
    b.line(2, 2, 5, 1, 16)
    b.set(3, 6, 1)
    b.set(5, 7, 1)
    return b


def make_beetle():
    b = Buf(16, 11)
    b.ellipse(9, 5, 6, 4, 23)
    b.ellipse(9, 4, 4, 2, 31)
    b.line(9, 1, 9, 9, 1)
    b.circle(3, 5, 2, 23)
    b.set(2, 4, 27)
    b.line(2, 3, 0, 1, 31)
    b.line(3, 2, 2, 0, 31)
    for x in (5, 8, 11, 13):
        b.line(x, 8, x - 1, 10, 31)
    return b


def make_spider():
    b = Buf(18, 12)
    b.ellipse(9, 6, 4, 3, 23)
    b.ellipse(9, 5, 2, 2, 31)
    b.circle(4, 5, 2, 23)
    b.set(3, 4, 29)
    b.set(5, 4, 29)
    for k, dy in enumerate((-3, -1, 1, 3)):
        b.line(7, 6, 2 + k, 6 + dy - 2, 23)
        b.line(11, 6, 16 - k, 6 + dy - 2, 23)
    return b


def make_pile(kind=15):
    b = Buf(14, 9)
    b.ellipse(7, 6, 6, 3, kind)
    b.ellipse(7, 5, 4, 2, kind + 2 if kind + 2 in PAL else kind)
    b.set(5, 3, 26 if kind == 18 else 17)
    return b


def make_throne():
    b = Buf(22, 16)
    b.rect(2, 12, 18, 4, 16)
    b.rect(3, 13, 16, 2, 17)
    b.rect(3, 2, 3, 11, 16)
    b.rect(16, 2, 3, 11, 16)
    b.rect(3, 2, 16, 2, 17)
    b.circle(11, 4, 2, 8)
    b.set(11, 4, 27)
    return b


def make_mound():
    b = Buf(72, 26)
    # monte de terra com textura
    for y in range(26):
        wdt = int((y / 25.0) ** 1.6 * 34) + 2
        b.rect(36 - wdt, y, wdt * 2, 1, 33)
    for y in range(0, 26, 2):
        wdt = int((y / 25.0) ** 1.6 * 30)
        if wdt > 3:
            b.hline(36 - wdt + 2, y, wdt, 34)
    # buraco da entrada
    b.ellipse(36, 4, 6, 3, 35)
    b.ellipse(36, 3, 4, 2, 0)
    b.ellipse(36, 4, 5, 2, 35)
    # pedrinhas
    for (x, y) in [(14, 22), (55, 21), (25, 24), (48, 24)]:
        b.circle(x, y, 1, 19)
    return b


def make_grass():
    b = Buf(14, 8)
    for x in (1, 4, 7, 10, 13):
        b.line(x, 7, x + (1 if x % 3 else -1), 1, 10 if x % 2 else 11)
        b.line(x, 7, x, 3, 36)
    return b


def make_pebble():
    b = Buf(6, 5)
    b.ellipse(3, 2, 2, 2, 19)
    b.set(2, 1, 18)
    return b


def make_sun():
    b = Buf(15, 15)
    b.circle(7, 7, 4, 14)
    b.circle(7, 7, 3, 27)
    for k in range(8):
        import math
        a = k * math.pi / 4
        x1 = int(7 + 5.5 * math.cos(a)); y1 = int(7 + 5.5 * math.sin(a))
        x2 = int(7 + 7 * math.cos(a));   y2 = int(7 + 7 * math.sin(a))
        b.line(x1, y1, x2, y2, 14)
    return b


def make_cloud():
    b = Buf(26, 12)
    b.ellipse(8, 7, 7, 4, 25)
    b.ellipse(16, 6, 8, 5, 25)
    b.ellipse(12, 4, 5, 3, 26)
    b.ellipse(20, 8, 5, 3, 31)
    return b


def make_moon():
    b = Buf(14, 14)
    b.circle(7, 7, 6, 25)
    b.circle(10, 5, 6, 0)
    b.circle(4, 6, 1, 31)
    b.circle(5, 10, 1, 31)
    return b


def make_heart():
    return art_to_buf("""
.99..99.
99999999
99999999
.999999.
..9999..
...99...
""")


def make_zzz():
    return art_to_buf("""
1111
..1.
.1..
1111
""", 4, 6)


def make_spark():
    return art_to_buf("""
..8..
.888.
88888
.888.
..8..
""")


# ------------------------------- ícones UI ---------------------------------
def ico_feed():
    b = Buf(12, 12)
    b.ellipse(6, 7, 5, 4, 15)          # monte de comida
    b.ellipse(6, 5, 4, 3, 17)
    b.circle(4, 4, 1, 12)
    b.circle(8, 4, 1, 11)
    b.set(6, 2, 26)
    return b


def ico_play():
    b = Buf(12, 12)
    b.circle(6, 6, 5, 30)
    b.line(5, 4, 8, 6, 26)
    b.line(5, 8, 8, 6, 26)
    b.vline(5, 4, 5, 26)
    b.set(9, 3, 37)
    return b


def ico_hunt():
    b = Buf(12, 12)
    b.line(1, 10, 10, 1, 16)           # lança
    b.line(8, 1, 10, 1, 17)
    b.line(10, 1, 10, 3, 17)
    b.ellipse(4, 8, 3, 2, 23)          # presa
    b.set(2, 7, 29)
    return b


def ico_rest():
    b = Buf(12, 12)
    b.rect(1, 6, 10, 3, 30)            # colchão
    b.rect(2, 4, 3, 2, 25)             # travesseiro
    b.vline(0, 3, 7, 31)
    b.vline(11, 5, 5, 31)
    b.hline(1, 9, 10, 31)
    b.set(8, 2, 25)                    # z
    b.set(9, 1, 25)
    return b


def ico_work():
    b = Buf(12, 12)
    b.line(2, 10, 8, 3, 16)            # picareta
    b.line(6, 2, 11, 5, 19)
    b.line(6, 2, 8, 4, 18)
    b.circle(2, 10, 1, 17)
    return b


def ico_pet():
    b = Buf(12, 12)
    b.ellipse(5, 8, 4, 3, 32)          # mão aberta
    b.line(2, 5, 2, 7, 32)
    b.line(4, 4, 4, 6, 32)
    b.line(6, 4, 6, 6, 32)
    b.line(8, 5, 8, 7, 32)
    b.circle(10, 3, 1, 9)              # coraçãozinho
    b.set(9, 2, 9); b.set(11, 2, 9)
    return b


def ico_clean():
    b = Buf(12, 12)
    b.rect(2, 6, 8, 4, 24)             # gota/água
    b.ellipse(6, 4, 2, 3, 24)
    b.set(5, 3, 26)
    b.line(1, 11, 11, 11, 31)
    return b


def ico_egg():
    b = Buf(12, 12)
    b.ellipse(6, 6, 3, 5, 20)
    b.set(5, 3, 26)
    b.ellipse(6, 9, 2, 2, 18)
    return b


def ico_home():
    b = Buf(12, 12)
    b.line(1, 6, 6, 1, 32)
    b.line(6, 1, 11, 6, 32)
    b.rect(2, 6, 8, 5, 32)
    b.rect(5, 8, 3, 3, 33)
    return b


def ico_tree():
    b = Buf(12, 12)
    b.rect(5, 7, 2, 5, 16)
    b.circle(6, 4, 4, 10)
    b.circle(4, 3, 2, 11)
    b.set(8, 3, 11)
    return b


def ico_gear():
    b = Buf(12, 12)
    b.circle(6, 6, 4, 31)
    b.circle(6, 6, 2, 0)
    for k in range(8):
        import math
        a = k * math.pi / 4
        b.set(int(6 + 5 * math.cos(a)), int(6 + 5 * math.sin(a)), 31)
    return b


def ico_back():
    b = Buf(12, 12)
    b.line(10, 6, 2, 6, 32)
    b.line(2, 6, 6, 2, 32)
    b.line(2, 6, 6, 10, 32)
    return b


def ico_clock():
    b = Buf(12, 12)
    b.circle(6, 6, 5, 31)
    b.circle(6, 6, 4, 0)
    b.vline(6, 3, 4, 32)
    b.hline(6, 6, 3, 32)
    return b


def ico_wifi():
    b = Buf(12, 12)
    b.line(2, 6, 4, 3, 30)
    b.line(10, 6, 8, 3, 30)
    b.line(4, 3, 8, 3, 30)
    b.line(3, 8, 5, 6, 30)
    b.line(9, 8, 7, 6, 30)
    b.line(5, 6, 7, 6, 30)
    b.circle(6, 10, 1, 30)
    return b


def ico_weather(kind):
    b = Buf(14, 12)
    if kind == 'sun':
        b.circle(7, 6, 4, 14)
        b.circle(7, 6, 2, 27)
    elif kind == 'cloud':
        b.ellipse(6, 6, 5, 3, 25)
        b.ellipse(10, 6, 3, 2, 31)
    elif kind == 'rain':
        b.ellipse(7, 4, 5, 3, 31)
        for x in (4, 7, 10):
            b.line(x, 8, x - 1, 11, 24)
    elif kind == 'storm':
        b.ellipse(7, 4, 5, 3, 31)
        b.line(7, 7, 5, 10, 14)
        b.line(5, 10, 8, 10, 14)
        b.line(8, 10, 6, 12, 14)
    elif kind == 'snow':
        b.ellipse(7, 4, 5, 3, 25)
        for x in (4, 7, 10):
            b.set(x, 9, 26); b.set(x, 11, 26)
    elif kind == 'fog':
        for y in (4, 7, 10):
            b.hline(1, y, 12, 31)
    return b


def ico_drop():
    b = Buf(10, 12)
    b.line(5, 1, 5, 4, 24)
    b.ellipse(5, 7, 3, 4, 24)
    b.set(4, 5, 26)
    return b


def ico_leaf():
    b = make_leaf()
    return b


def ico_mat():
    b = Buf(12, 12)
    b.line(1, 9, 10, 3, 16)
    b.line(1, 10, 10, 4, 17)
    b.line(6, 6, 8, 2, 16)
    b.circle(2, 10, 1, 19)
    return b


# ============================== montagem ====================================
def build_all():
    s = {}
    # formigas (2 quadros de caminhada + variantes)
    s['SPR_ANT_WALK_R'] = make_ant(18, 12, 1, 2, 3, 3, 0)
    s['SPR_ANT_WALK_L'] = s['SPR_ANT_WALK_R'].mirror()
    s['SPR_ANT_CARRY_R'] = None      # montado abaixo
    s['SPR_ANT_SLEEP'] = None
    s['SPR_ANT_EAT'] = None

    base = make_ant(18, 12, 1, 2, 3, 3, 1)
    s['SPR_ANT_WALK2_R'] = base
    s['SPR_ANT_WALK2_L'] = base.mirror()

    # carregando: folha sobre as costas
    c = make_ant(18, 14, 1, 2, 3, 3, 0)
    c.overlay(make_leaf(), 5, 0)
    s['SPR_ANT_CARRY_R'] = c
    s['SPR_ANT_CARRY_L'] = c.mirror()

    # dormindo: enroscada
    sl = Buf(15, 10)
    sl.ellipse(8, 6, 5, 3, 1)
    sl.ellipse(7, 5, 3, 2, 2)
    sl.circle(3, 6, 2, 1)
    sl.set(2, 5, 2)
    sl.line(11, 8, 14, 9, 3)
    s['SPR_ANT_SLEEP'] = sl

    # comendo
    ea = make_ant(18, 12, 1, 2, 3, 3, 0)
    ea.circle(1, 9, 1, 15)
    ea.set(0, 8, 17)
    ea.set(2, 8, 15)
    s['SPR_ANT_EAT'] = ea

    # rainha
    q = make_queen(0)
    q.overlay(make_crown(), 1, 3)
    s['SPR_QUEEN_IDLE'] = q
    q2 = make_queen(1)
    q2.overlay(make_crown(), 1, 3)
    s['SPR_QUEEN_WALK'] = q2
    qh = make_queen(0)
    qh.overlay(make_crown(), 1, 3)
    qh.set(2, qh.h - 6, 9)             # bochecha "feliz"
    qh.set(3, qh.h - 5, 9)
    s['SPR_QUEEN_HAPPY'] = qh
    # dormindo: enroscada, gaster grande + cabeça apoiada
    qs = Buf(24, 11)
    qs.ellipse(14, 6, 8, 4, 5)
    qs.ellipse(13, 5, 6, 3, 6)
    qs.line(8, 3, 20, 3, 7)
    qs.line(9, 5, 19, 5, 7)
    qs.circle(4, 7, 3, 5)
    qs.set(3, 6, 6)
    qs.line(20, 9, 23, 10, 1)          # perna dobrada
    s['SPR_QUEEN_SLEEP'] = qs
    s['SPR_CROWN'] = make_crown()
    s['SPR_HEART'] = make_heart()
    s['SPR_ZZZ'] = make_zzz()
    s['SPR_SPARK'] = make_spark()

    # recursos
    s['SPR_LEAF'] = make_leaf()
    s['SPR_LEAF_PILE'] = make_leaf_pile()
    s['SPR_BERRY'] = make_berry()
    s['SPR_BUSH'] = make_bush()
    s['SPR_FLOWER_R'] = make_flower(13, 14)
    s['SPR_FLOWER_Y'] = make_flower(14, 27)
    s['SPR_SEEDS'] = make_seeds()
    s['SPR_TWIG'] = make_twig()
    s['SPR_MUSHROOM'] = make_mushroom()
    s['SPR_EGG'] = make_egg()
    s['SPR_LARVA'] = make_larva()
    s['SPR_PUPA'] = make_pupa()
    s['SPR_BEETLE'] = make_beetle()
    s['SPR_SPIDER'] = make_spider()
    s['SPR_PILE_FOOD'] = make_pile(15)
    s['SPR_PILE_MAT'] = make_pile(16)
    s['SPR_THRONE'] = make_throne()
    s['SPR_MOUND'] = make_mound()
    s['SPR_GRASS'] = make_grass()
    s['SPR_PEBBLE'] = make_pebble()
    s['SPR_SUN'] = make_sun()
    s['SPR_CLOUD'] = make_cloud()
    s['SPR_MOON'] = make_moon()

    # ícones
    s['SPR_ICO_FEED'] = ico_feed()
    s['SPR_ICO_PLAY'] = ico_play()
    s['SPR_ICO_HUNT'] = ico_hunt()
    s['SPR_ICO_REST'] = ico_rest()
    s['SPR_ICO_WORK'] = ico_work()
    s['SPR_ICO_PET'] = ico_pet()
    s['SPR_ICO_CLEAN'] = ico_clean()
    s['SPR_ICO_EGG'] = ico_egg()
    s['SPR_ICO_HOME'] = ico_home()
    s['SPR_ICO_TREE'] = ico_tree()
    s['SPR_ICO_GEAR'] = ico_gear()
    s['SPR_ICO_BACK'] = ico_back()
    s['SPR_ICO_CLOCK'] = ico_clock()
    s['SPR_ICO_WIFI'] = ico_wifi()
    s['SPR_ICO_RAIN'] = ico_weather('rain')
    s['SPR_ICO_STORM'] = ico_weather('storm')
    s['SPR_ICO_SNOW'] = ico_weather('snow')
    s['SPR_ICO_FOG'] = ico_weather('fog')
    s['SPR_ICO_DROP'] = ico_drop()
    s['SPR_ICO_LEAF'] = make_leaf().trim()
    s['SPR_ICO_MAT'] = ico_mat()
    return s


# ============================== emissão C ===================================
def rgb565_swapped(rgb):
    r, g, b = rgb
    v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3)
    return ((v >> 8) | (v << 8)) & 0xFFFF


def emit_c(sprites, order):
    out = []
    out.append("/* Gerado automaticamente por tools/gen_sprites.py - NAO EDITAR */")
    out.append('#include <string.h>')
    out.append('#include "sprites.h"')
    out.append("")
    out.append("/* paletas (RGB565 com bytes já trocados para o ILI9341) */")
    for name in order:
        buf = sprites[name]
        used = sorted({buf.get(x, y) for y in range(buf.h) for x in range(buf.w)} - {0})
        # mantém o índice original para não precisar remapear os pixels
        maxidx = max(used) if used else 1
        out.append("static const uint16_t pal_%s[] = {" % name.lower())
        vals = ["0x0000"] + ["0x%04X" % rgb565_swapped(PAL.get(i, (0, 0, 0)))
                             for i in range(1, maxidx + 1)]
        for i in range(0, len(vals), 8):
            out.append("    " + ", ".join(vals[i:i + 8]) + ",")
        out.append("};")
    out.append("")
    out.append("/* pixels: 1 byte por pixel (índice na paleta; 0 = transparente) */")
    for name in order:
        buf = sprites[name]
        out.append("static const uint8_t px_%s[] = { /* %dx%d */" %
                   (name.lower(), buf.w, buf.h))
        row = []
        for y in range(buf.h):
            row.append("    " + ",".join("%d" % buf.get(x, y) for x in range(buf.w)) + ",")
        out.extend(row)
        out.append("};")
    out.append("")
    out.append("const sprite_t g_sprites[SPR_COUNT] = {")
    for name in order:
        buf = sprites[name]
        out.append("    [%s] = { %d, %d, pal_%s, px_%s }," %
                   (name, buf.w, buf.h, name.lower(), name.lower()))
    out.append("};")
    out.append("")
    out.append("/* ---------------- blitter ---------------- */")
    out.append("""
void spr_draw(gfx_canvas_t *c, sprite_id_t id, int x, int y)
{
    const sprite_t *s = &g_sprites[id];
    if (!s->px) return;
    int x1 = x + s->w - 1, y1 = y + s->h - 1;
    if (x1 < c->cx0 || x > c->cx1 || y1 < c->cy0 || y > c->cy1) return;
    int sy0 = y < c->cy0 ? c->cy0 : y;
    int sy1 = y1 > c->cy1 ? c->cy1 : y1;
    int sx0 = x < c->cx0 ? c->cx0 : x;
    int sx1 = x1 > c->cx1 ? c->cx1 : x1;
    for (int yy = sy0; yy <= sy1; yy++) {
        const uint8_t *row = s->px + (size_t)(yy - y) * s->w;
        uint16_t *dst = c->buf + (yy - c->y0) * c->w + (sx0 - c->x0);
        for (int xx = sx0; xx <= sx1; xx++) {
            uint8_t i = row[xx - x];
            if (i) *dst = s->pal[i];
            dst++;
        }
    }
}

void spr_draw_flip(gfx_canvas_t *c, sprite_id_t id, int x, int y)
{
    const sprite_t *s = &g_sprites[id];
    if (!s->px) return;
    int x1 = x + s->w - 1, y1 = y + s->h - 1;
    if (x1 < c->cx0 || x > c->cx1 || y1 < c->cy0 || y > c->cy1) return;
    for (int yy = (y < c->cy0 ? c->cy0 : y); yy <= (y1 > c->cy1 ? c->cy1 : y1); yy++) {
        const uint8_t *row = s->px + (size_t)(yy - y) * s->w;
        for (int xx = (x < c->cx0 ? c->cx0 : x); xx <= (x1 > c->cx1 ? c->cx1 : x1); xx++) {
            uint8_t i = row[s->w - 1 - (xx - x)];
            if (i) gfx_pixel(c, xx, yy, s->pal[i]);
        }
    }
}

void spr_draw_centered(gfx_canvas_t *c, sprite_id_t id, int cx, int cy)
{
    const sprite_t *s = &g_sprites[id];
    spr_draw(c, id, cx - s->w / 2, cy - s->h / 2);
}

bool spr_hit(sprite_id_t id, int cx, int cy, int px, int py, int pad)
{
    const sprite_t *s = &g_sprites[id];
    int x0 = cx - s->w / 2 - pad, y0 = cy - s->h / 2 - pad;
    return px >= x0 && px < x0 + s->w + 2 * pad &&
           py >= y0 && py < y0 + s->h + 2 * pad;
}
""")
    with open(OUT_C, "w") as f:
        f.write("\n".join(out) + "\n")


# ============================== BMP (preview) ==============================
def write_bmp(path, w, h, get_rgb):
    row = bytearray()
    for y in range(h - 1, -1, -1):
        line = bytearray()
        for x in range(w):
            r, g, b = get_rgb(x, y)
            line += bytes((b, g, r))
        pad = (-len(line)) % 4
        row += line + b"\x00" * pad
    size = 54 + len(row)
    with open(path, "wb") as f:
        f.write(b"BM")
        f.write(struct.pack("<IHHI", size, 0, 0, 54))
        f.write(struct.pack("<IIIHHIIIIII", 40, w, h, 1, 24, 0, len(row),
                            2835, 2835, 0, 0))
        f.write(bytes(row))


def write_png(path, w, h, get_rgb):
    """PNG 24-bit sem dependências externas (zlib é da stdlib)."""
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            r, g, b = get_rgb(x, y)
            raw += bytes((r & 255, g & 255, b & 255))
    comp = zlib.compress(bytes(raw), 6)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", comp))
        f.write(chunk(b"IEND", b""))


def contact_sheet(sprites, order, path, scale=5):
    cols = 8
    cell = 30 * scale
    rows = (len(order) + cols - 1) // cols
    W, H = cols * cell, rows * (cell + 4 * scale)

    def get(x, y):
        cx, cy = x // cell, y // (cell + 4 * scale)
        i = cy * cols + cx
        if i >= len(order):
            return (24, 20, 18)
        buf = sprites[order[i]]
        lx = (x % cell - (cell - buf.w * scale) // 2)
        ly = (y % (cell + 4 * scale) - 2 * scale)
        if 0 <= lx < buf.w * scale and 0 <= ly < buf.h * scale:
            idx = buf.get(lx // scale, ly // scale)
            if idx:
                return PAL.get(idx, (255, 0, 255))
        # fundo xadrez
        return (38, 32, 28) if ((x // scale + y // scale) % 2) else (30, 25, 22)

    write_png(path, W, H, get)
    return W, H


# ============================== prévia das cenas ============================
def preview_scenes(sprites, order):
    """Renderiza uma aproximação das duas telas do jogo em BMP (só para
    conferência visual da arte e da composição — a lógica real é C)."""
    W, H = 320, 240

    def scene(out, kind):
        def px(x, y, rgb):
            if 0 <= x < W and 0 <= y < H:
                out[y][x] = rgb

        def rect(x, y, w, h, rgb):
            for yy in range(y, y + h):
                for xx in range(x, x + w):
                    px(xx, yy, rgb)

        def blit(name, x, y, flip=False):
            b = sprites[name]
            for yy in range(b.h):
                for xx in range(b.w):
                    i = b.get(b.w - 1 - xx if flip else xx, yy)
                    if i:
                        px(x + xx, y + yy, PAL.get(i, (255, 0, 255)))

        def circle(cx, cy, r, rgb):
            for dy in range(-r, r + 1):
                for dx in range(-r, r + 1):
                    if dx * dx + dy * dy <= r * r:
                        px(cx + dx, cy + dy, rgb)

        def thick_line(x0, y0, x1, y1, t, rgb):
            steps = max(abs(x1 - x0), abs(y1 - y0), 1)
            for i in range(steps + 1):
                x = x0 + (x1 - x0) * i // steps
                y = y0 + (y1 - y0) * i // steps
                rect(x - t // 2, y - t // 2, t, t, rgb)

        rect(0, 0, W, H, (18, 14, 12))
        # barra de status
        rect(0, 0, W, 24, (40, 30, 24))
        rect(0, 23, W, 1, (96, 74, 54))
        # barra inferior
        rect(0, 202, W, 38, (40, 30, 24))
        rect(0, 202, W, 1, (96, 74, 54))

        if kind == 'colony':
            # céu
            for y in range(24, 46):
                k = (y - 24) / 22.0
                rect(0, y, W, 1, (int(110 - 40 * k), int(178 - 60 * k), int(232 - 60 * k)))
            blit('SPR_SUN', 288, 26)
            blit('SPR_CLOUD', 40, 28)
            # solo
            rect(0, 46, W, 196 - 46, (78, 50, 30))
            rect(0, 46, W, 3, (122, 196, 78))
            for x in range(0, W, 14):
                blit('SPR_GRASS', x, 40)
            blit('SPR_MOUND', 124, 24)
            # túneis
            T = (30, 18, 12)
            thick_line(160, 30, 160, 54, 14, (116, 78, 46))
            thick_line(160, 30, 160, 54, 12, T)
            for (a, b, c, d, t) in [(160, 54, 62, 75, 12), (160, 54, 256, 75, 12),
                                    (62, 75, 62, 126, 12), (256, 75, 160, 134, 12),
                                    (62, 126, 160, 134, 12), (160, 54, 268, 48, 9),
                                    (268, 48, 256, 75, 9)]:
                thick_line(a, b, c, d, t + 2, (116, 78, 46))
                thick_line(a, b, c, d, t, T)
            # câmaras
            def chamber(x, y, w, h):
                rect(x - 1, y - 1, w + 2, h + 2, (116, 78, 46))
                rect(x, y, w, h, T)
                rect(x + 1, y + h - 3, w - 2, 2, (58, 36, 24))
            chamber(28, 58, 72, 40)
            chamber(218, 58, 76, 40)
            chamber(26, 110, 76, 40)
            chamber(116, 116, 96, 44)
            chamber(244, 34, 46, 28)
            chamber(138, 44, 44, 22)
            # conteúdo
            blit('SPR_PILE_FOOD', 36, 84); blit('SPR_PILE_MAT', 66, 84)
            blit('SPR_MUSHROOM', 232, 86); blit('SPR_MUSHROOM', 248, 88); blit('SPR_MUSHROOM', 264, 86)
            blit('SPR_EGG', 36, 136); blit('SPR_EGG', 46, 138); blit('SPR_LARVA', 60, 136)
            blit('SPR_THRONE', 150, 138)
            blit('SPR_QUEEN_IDLE', 152, 134)
            blit('SPR_ANT_WALK_R', 150, 60)
            blit('SPR_ANT_WALK_L', 96, 96)
            blit('SPR_ANT_CARRY_R', 230, 120)
            blit('SPR_ANT_SLEEP', 40, 128)
            blit('SPR_ANT_WALK_R', 258, 42)
        else:
            for y in range(24, 100):
                k = (y - 24) / 76.0
                rect(0, y, W, 1, (int(110 - 20 * k), int(178 - 20 * k), int(232 - 30 * k)))
            blit('SPR_SUN', 282, 28)
            blit('SPR_CLOUD', 30, 30)
            blit('SPR_CLOUD', 150, 44)
            # colinas
            for cx in (40, 130, 240, 320):
                circle(cx, 104, 34, (76, 120, 60))
            rect(0, 100, W, 102, (88, 150, 62))
            rect(0, 100, W, 4, (122, 196, 78))
            rect(0, 160, W, 42, (78, 50, 30))
            rect(0, 160, W, 3, (44, 96, 38))
            blit('SPR_MOUND', 0, 138)
            for x in range(0, W, 22):
                blit('SPR_GRASS', x + 4, 96)
            blit('SPR_LEAF_PILE', 90, 150)
            blit('SPR_BUSH', 130, 140)
            blit('SPR_FLOWER_R', 186, 146)
            blit('SPR_FLOWER_Y', 200, 148)
            blit('SPR_TWIG', 230, 154)
            blit('SPR_SEEDS', 268, 154)
            blit('SPR_MUSHROOM', 300, 150)
            blit('SPR_ANT_CARRY_R', 112, 152)
            blit('SPR_ANT_WALK_R', 168, 154)
            blit('SPR_BEETLE', 258, 148)
            blit('SPR_SPIDER', 296, 132)
        return out

    grid = [[(0, 0, 0)] * W for _ in range(H)]
    scene(grid, 'colony')
    write_png(os.path.join(DOCS, "scene_colony.png"), W, H,
              lambda x, y: grid[y][x])
    grid2 = [[(0, 0, 0)] * W for _ in range(H)]
    scene(grid2, 'outside')
    write_png(os.path.join(DOCS, "scene_outside.png"), W, H,
              lambda x, y: grid2[y][x])


def main():
    os.makedirs(DOCS, exist_ok=True)
    sprites = build_all()

    # ordem = enum em sprites.h
    hdr = os.path.join(HERE, "..", "main", "game", "sprites.h")
    order = []
    with open(hdr) as f:
        inside = False
        for line in f:
            if line.strip().startswith("typedef enum"):
                inside = True
                continue
            if inside:
                if "}" in line:
                    break
                name = line.strip().replace(",", "").split("=")[0].strip()
                if name.startswith("SPR_") and name != "SPR_COUNT":
                    order.append(name)
    # frames extras de animação (não fazem parte do enum público)
    extra = [n for n in ("SPR_ANT_WALK2_R", "SPR_ANT_WALK2_L") if n not in order]
    missing = [n for n in order if n not in sprites]
    if missing:
        raise SystemExit("sprites faltando: %s" % missing)

    emit_c(sprites, order + extra)
    w, h = contact_sheet(sprites, order + extra, os.path.join(DOCS, "sprites.png"))
    preview_scenes(sprites, order)
    total = sum(sprites[n].w * sprites[n].h for n in order + extra)
    print("OK: %d sprites, %d bytes de pixels, folha %dx%d" %
          (len(order) + len(extra), total, w, h))


if __name__ == "__main__":
    main()
