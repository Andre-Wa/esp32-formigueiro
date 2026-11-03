/*
 * colony.c — Simulação do habitat: tempo, rotina, economia, IA das formigas,
 * rainha (o "pet"), clima, partículas e persistência.
 *
 * Tudo roda em números inteiros (ponto fixo Q8 para posições) e é chamado
 * uma única vez por tick a partir da tarefa do jogo (Core 1), portanto não
 * há concorrência nem necessidade de mutex.
 *
 * COORDENADAS:
 *  - interior: px da vista (0..319, 0..177) -> tela = y + VIEW_Y
 *  - exterior: px do MUNDO (0..639)          -> tela = x - cam_x, y + VIEW_Y
 *  - partículas: já em coordenadas de TELA
 */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "colony.h"
#include "gfx_ext.h"
#include "weather.h"
#include "game_save.h"

static const char *TAG = "colony";

colony_t g_col;

/* flags de intenção das operárias */
#define F_BUILD  0x08     /* indo executar a obra na câmara selecionada */
#define F_LEAVE  0x10     /* ao chegar no nó 0, sai para o exterior */
#define F_FREE   0x20     /* andando livre dentro da câmara */
#define F_EAT    0x40     /* indo comer na despensa */
#define F_SLEEP  0x80     /* indo dormir no berçário */

/* ============================ grafo de túneis ============================= */
/* Nós de navegação (coordenadas da VISTA INTERIOR, em px):
 *   0 entrada do túnel  1 despensa  2 jardim de fungos  3 berçário
 *   4 câmara real       5 átrio     6 lixeira           7 superfície (mundo)
 */
const int16_t g_node_x[COLONY_MAX_NODES] = { 160,  62, 256,  62, 160, 160, 268, 160 };
const int16_t g_node_y[COLONY_MAX_NODES] = {  32,  75,  75, 126, 134,  54,  48,   6 };
const uint8_t g_node_count = 8;

static const uint8_t ADJ[12][2] = {
    { 0, 5 }, { 1, 5 }, { 2, 5 }, { 5, 6 },
    { 1, 3 }, { 3, 4 }, { 2, 4 }, { 6, 2 },
    { 7, 0 }, { 3, 5 }, { 4, 5 }, { 1, 4 },
};
#define ADJ_N ((int)(sizeof(ADJ) / sizeof(ADJ[0])))

static uint8_t s_dist[COLONY_MAX_NODES][COLONY_MAX_NODES];

/* estado de sessão (não vai no save): vida da aranha, obra em andamento,
 * teto da equipe externa e prioridade manual da câmera */
static uint8_t  s_spider_hp    = 100;
static uint32_t s_spider_back_at = 0;
static int8_t   s_build_ci     = -1;
static uint8_t  s_forage_cap   = 3;
static uint16_t s_cam_hold     = 0;

static void build_dist(void)
{
    for (int i = 0; i < COLONY_MAX_NODES; i++) {
        for (int j = 0; j < COLONY_MAX_NODES; j++) {
            s_dist[i][j] = (i == j) ? 0 : 200;
        }
    }
    for (int k = 0; k < ADJ_N; k++) {
        s_dist[ADJ[k][0]][ADJ[k][1]] = 1;
        s_dist[ADJ[k][1]][ADJ[k][0]] = 1;
    }
    for (int k = 0; k < COLONY_MAX_NODES; k++) {      /* Floyd-Warshall */
        for (int i = 0; i < COLONY_MAX_NODES; i++) {
            for (int j = 0; j < COLONY_MAX_NODES; j++) {
                if (s_dist[i][k] + s_dist[k][j] < s_dist[i][j]) {
                    s_dist[i][j] = (uint8_t)(s_dist[i][k] + s_dist[k][j]);
                }
            }
        }
    }
}

int colony_find_path(uint8_t from, uint8_t to, int8_t *out, int cap)
{
    if (from >= g_node_count || to >= g_node_count || cap < 2) return 0;
    out[0] = (int8_t)from;
    int n = 1;
    uint8_t cur = from;
    while (cur != to && n < cap) {
        uint8_t best = cur, bd = 250;
        for (int k = 0; k < ADJ_N; k++) {
            int nb = -1;
            if (ADJ[k][0] == cur) nb = ADJ[k][1];
            else if (ADJ[k][1] == cur) nb = ADJ[k][0];
            if (nb < 0) continue;
            if (s_dist[nb][to] < bd) { bd = s_dist[nb][to]; best = (uint8_t)nb; }
        }
        if (best == cur) break;
        cur = best;
        out[n++] = (int8_t)cur;
    }
    return n;
}

/* ============================ RNG ========================================= */
uint32_t colony_rand(void)
{
    uint32_t x = g_col.rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g_col.rng = x ? x : 0x12345678u;
    return g_col.rng;
}

uint32_t colony_rand_range(uint32_t n) { return n ? (colony_rand() % n) : 0; }

static int rnd_range(int a, int b)
{
    return (b <= a) ? a : a + (int)colony_rand_range((uint32_t)(b - a + 1));
}
static bool chance(int pct) { return (int)colony_rand_range(100) < pct; }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ============================ tempo ======================================= */
uint8_t colony_hour(void)   { return (uint8_t)(g_col.sec_of_day / 3600); }
uint8_t colony_minute(void) { return (uint8_t)((g_col.sec_of_day % 3600) / 60); }

colony_phase_t colony_phase(void)
{
    uint8_t h = colony_hour();
    if (h >= 21 || h < 5)  return PHASE_NIGHT;
    if (h < 9)             return PHASE_DAWN;
    if (h < 16)            return PHASE_DAY;
    if (h < 19)            return PHASE_AFTERNOON;
    return PHASE_EVENING;
}

bool colony_is_night(void) { return colony_phase() == PHASE_NIGHT; }
bool colony_raining(void)
{
    return g_col.weather == WX_RAIN || g_col.weather == WX_STORM;
}

const char *colony_phase_name(void)
{
    switch (colony_phase()) {
    case PHASE_NIGHT:     return "Noite";
    case PHASE_DAWN:      return "Manha";
    case PHASE_DAY:       return "Coleta";
    case PHASE_AFTERNOON: return "Obras";
    default:              return "Jantar";
    }
}

uint8_t colony_population(void) { return (uint8_t)(g_col.n_workers + 1); }

/* ============================ mensagens =================================== */
void colony_event(const char *fmt, ...)
{
    char tmp[40];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    uint8_t i = g_col.event_head;
    strlcpy(g_col.events[i], tmp, sizeof(g_col.events[i]));
    g_col.event_head = (uint8_t)((i + 1) % COLONY_MAX_EVENTS);
    if (g_col.event_count < COLONY_MAX_EVENTS) g_col.event_count++;
}

void colony_msg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_col.msg, sizeof(g_col.msg), fmt, ap);
    va_end(ap);
    g_col.msg_ticks = 140;             /* ~7 s */
    colony_event("%s", g_col.msg);
}

/* ============================ partículas ================================== */
static void part_add(uint8_t kind, int x, int y, int vx, int vy, uint8_t life,
                     uint16_t col, const char *txt)
{
    if (g_col.n_parts >= COLONY_MAX_PARTS) return;
    particle_t *p = &g_col.parts[g_col.n_parts++];
    memset(p, 0, sizeof(*p));
    p->kind = kind;
    p->x = (int16_t)x; p->y = (int16_t)y;
    p->vx = (int8_t)vx; p->vy = (int8_t)vy;
    p->life = p->max_life = life;
    p->col = col;
    if (txt) strlcpy(p->txt, txt, sizeof(p->txt));
}

void colony_fx_heart(int x, int y) { part_add(P_HEART, x, y, rnd_range(-6, 6), -14, 46, COL_HEART, NULL); }
void colony_fx_spark(int x, int y, uint16_t col) { part_add(P_SPARK, x, y, rnd_range(-18, 18), rnd_range(-22, -6), 24, col, NULL); }
void colony_fx_dirt(int x, int y)  { part_add(P_DIRT, x, y, rnd_range(-14, 14), rnd_range(-24, -8), 20, COL_SOIL_LIGHT, NULL); }
void colony_fx_zzz(int x, int y)   { part_add(P_ZZZ, x, y, 4, -10, 60, COL_UI_MUTED, NULL); }
void colony_fx_text(int x, int y, const char *txt, uint16_t col) { part_add(P_TEXT, x, y, 0, -12, 44, col, txt); }

/* atalhos que convertem coordenada local da vista -> tela */
#define FX(x, y)  (x), (y) + VIEW_Y

static void parts_update(void)
{
    for (int i = 0; i < g_col.n_parts; ) {
        particle_t *p = &g_col.parts[i];
        p->x = (int16_t)(p->x + (p->vx >> 3));
        p->y = (int16_t)(p->y + (p->vy >> 3));
        if (p->kind != P_HEART && p->kind != P_ZZZ && p->kind != P_TEXT) {
            p->vy = (int8_t)clampi(p->vy + 2, -127, 127);
        }
        if (p->life) p->life--;
        if (!p->life) g_col.parts[i] = g_col.parts[--g_col.n_parts];
        else i++;
    }
}

/* ============================ câmaras ===================================== */
static const char *ch_name(uint8_t kind)
{
    switch (kind) {
    case CH_ATRIUM:  return "Atrio";
    case CH_STORAGE: return "Despensa";
    case CH_FUNGUS:  return "Jardim de fungos";
    case CH_NURSERY: return "Bercario";
    case CH_ROYAL:   return "Camara real";
    case CH_TRASH:   return "Lixeira";
    default:         return "Tunel";
    }
}

const char *colony_chamber_name(int idx)
{
    if (idx < 0 || idx >= g_col.n_chambers) return "-";
    return ch_name(g_col.chambers[idx].kind);
}

static chamber_t *ch_add(uint8_t kind, int x, int y, int w, int h, uint8_t lvl, uint8_t node)
{
    chamber_t *c = &g_col.chambers[g_col.n_chambers++];
    memset(c, 0, sizeof(*c));
    c->kind = kind; c->x = (int16_t)x; c->y = (int16_t)y;
    c->w = (int16_t)w; c->h = (int16_t)h;
    c->cx = (int16_t)(x + w / 2); c->cy = (int16_t)(y + h / 2);
    c->level = lvl; c->node = node;
    return c;
}

static int ch_index(uint8_t kind)
{
    for (int i = 0; i < g_col.n_chambers; i++) {
        if (g_col.chambers[i].kind == kind) return i;
    }
    return -1;
}

uint8_t colony_food_cap(void)
{
    int i = ch_index(CH_STORAGE);
    uint8_t lvl = (i >= 0) ? g_col.chambers[i].level : 1;
    return (uint8_t)(120 + 60 * (lvl - 1));
}

uint8_t colony_worker_cap(void)
{
    int i = ch_index(CH_NURSERY);
    uint8_t lvl = (i >= 0) ? g_col.chambers[i].level : 1;
    return (uint8_t)(8 + 6 * (lvl - 1));      /* 8 / 14 / 20 */
}

uint8_t colony_brood_cap(void)
{
    int i = ch_index(CH_NURSERY);
    uint8_t lvl = (i >= 0) ? g_col.chambers[i].level : 1;
    return (uint8_t)(4 + 4 * (lvl - 1));      /* 4 / 8 / 12 */
}

uint8_t colony_build_cost(int chamber_idx)
{
    if (chamber_idx < 0 || chamber_idx >= g_col.n_chambers) return 0;
    uint8_t lvl = g_col.chambers[chamber_idx].level;
    return lvl >= 3 ? 0 : (uint8_t)(30 + 30 * (lvl - 1));   /* 30 / 60 */
}

/* ============================ recursos externos =========================== */
static void res_add(uint8_t kind, int x, int y, uint8_t amount, uint8_t respawn)
{
    if (g_col.n_res >= COLONY_MAX_RES) return;
    resource_t *r = &g_col.res[g_col.n_res++];
    memset(r, 0, sizeof(*r));
    r->kind = kind; r->x = (int16_t)x; r->y = (int16_t)y;
    r->amount = r->max_amount = amount;
    r->respawn_h = respawn;
}

void colony_res_yield(uint8_t kind, uint8_t *food, uint8_t *mat, uint8_t *ticks)
{
    switch (kind) {
    case RES_LEAF:     *food = 1; *mat = 3; *ticks = 50;  break;
    case RES_BERRY:    *food = 6; *mat = 0; *ticks = 70;  break;
    case RES_FLOWER:   *food = 3; *mat = 1; *ticks = 60;  break;
    case RES_SEED:     *food = 4; *mat = 1; *ticks = 65;  break;
    case RES_TWIG:     *food = 0; *mat = 5; *ticks = 80;  break;
    case RES_MUSHROOM: *food = 4; *mat = 0; *ticks = 55;  break;
    case RES_STONE:    *food = 0; *mat = 3; *ticks = 90;  break;
    case RES_PREY:     *food = 14; *mat = 1; *ticks = 110; break;
    default:           *food = 1; *mat = 1; *ticks = 50;  break;
    }
    if (g_col.weather == WX_RAIN && (kind == RES_FLOWER || kind == RES_MUSHROOM ||
                                     kind == RES_LEAF)) { (*food)++; (*mat)++; }
    if (g_col.weather == WX_SUN && kind == RES_BERRY) (*food) += 2;
    if (g_col.weather == WX_FOG && *food) (*food)--;
}

const char *colony_res_name(uint8_t kind)
{
    switch (kind) {
    case RES_LEAF:     return "folhas";
    case RES_BERRY:    return "frutas";
    case RES_FLOWER:   return "flores";
    case RES_SEED:     return "sementes";
    case RES_TWIG:     return "gravetos";
    case RES_MUSHROOM: return "cogumelos";
    case RES_STONE:    return "pedrinhas";
    case RES_PREY:     return "uma presa";
    default:           return "nada";
    }
}

/* ============================ fala da rainha ============================== */
static const char *SAY_HAPPY[]  = { "Que delicia de colonia!", "Obrigada pelo carinho!",
                                    "Estou otima hoje!", "Sinto-me amada!" };
static const char *SAY_HUNGRY[] = { "Estou com fome...", "Preciso de comida!",
                                    "A despensa esta vazia?" };
static const char *SAY_TIRED[]  = { "Preciso descansar...", "Que sono...",
                                    "Deixa eu dormir um pouco?" };
static const char *SAY_DIRTY[]  = { "Que sujeira aqui!", "Preciso de um banho!",
                                    "Mande limpar a colonia." };
static const char *SAY_SAD[]    = { "Sinto falta de voce.", "Ninguem brinca comigo...",
                                    "Estou triste hoje." };
static const char *SAY_NIGHT[]  = { "Boa noite...", "Escuro e quentinho..." };
static const char *SAY_EGG[]    = { "Vou por ovos!", "Mais formiguinhas vindo!" };
static const char *SAY_PET[]    = { "Hihi, que cosquinha!", "Mais carinho, por favor!",
                                    "Voce e o melhor humano!", "Obrigada!" };

static void queen_say(const char *s)
{
    strlcpy(g_col.queen.say, s ? s : "", sizeof(g_col.queen.say));
    g_col.queen.say_timer = 110;
}

static void queen_say_auto(void)
{
    queen_t *q = &g_col.queen;
    const char **t; int n;
    if (colony_is_night())    { t = SAY_NIGHT;  n = 2; }
    else if (q->hunger < 30)  { t = SAY_HUNGRY; n = 3; }
    else if (q->energy < 30)  { t = SAY_TIRED;  n = 3; }
    else if (q->hygiene < 35) { t = SAY_DIRTY;  n = 3; }
    else if (q->mood < 35)    { t = SAY_SAD;    n = 3; }
    else                      { t = SAY_HAPPY;  n = 4; }
    queen_say(t[colony_rand_range((uint32_t)n)]);
}

/* ============================ economia ==================================== */
static void add_food(int n)     { g_col.food = (int16_t)clampi(g_col.food + n, 0, colony_food_cap()); }
static void add_material(int n) { g_col.material = (int16_t)clampi(g_col.material + n, 0, 300); }

/* ============================ novo jogo =================================== */
void colony_new(void)
{
    uint32_t seed = 0x9E3779B9u ^ esp_random();
    memset(&g_col, 0, sizeof(g_col));
    build_dist();
    g_col.rng = seed ? seed : 0x12345678u;

    g_col.day = 1;
    g_col.sec_of_day = 8 * 3600;              /* começa às 08:00 */
    g_col.speed = 1;
    g_col.food = 30;
    g_col.material = 12;
    g_col.hygiene = 80;
    g_col.eggs = 2;
    g_col.larvae = 1;
    g_col.weather = WX_SUN;
    g_col.temp_x10 = 240;
    g_col.humidity = 55;

    ch_add(CH_STORAGE,  28,  58,  72,  40, 1, 1);
    ch_add(CH_FUNGUS,  218,  58,  76,  40, 1, 2);
    ch_add(CH_NURSERY,  26, 110,  76,  40, 1, 3);
    ch_add(CH_ROYAL,   116, 116,  96,  44, 1, 4);
    ch_add(CH_TRASH,   244,  34,  46,  28, 1, 6);
    ch_add(CH_ATRIUM,  138,  44,  44,  22, 1, 5);

    res_add(RES_LEAF,      96, 112, 4,  8);
    res_add(RES_BERRY,    146, 106, 3, 12);
    res_add(RES_FLOWER,   196, 110, 3, 10);
    res_add(RES_TWIG,     238, 114, 3, 14);
    res_add(RES_SEED,     282, 112, 4,  9);
    res_add(RES_MUSHROOM, 322, 114, 2, 12);
    res_add(RES_LEAF,     366, 112, 4,  8);
    res_add(RES_STONE,    404, 116, 3, 16);
    res_add(RES_BERRY,    450, 106, 3, 12);
    res_add(RES_FLOWER,   502, 110, 3, 10);
    res_add(RES_PREY,     552, 112, 1, 20);
    res_add(RES_SEED,     598, 112, 4,  9);

    g_col.spider_x = 520; g_col.spider_y = 104; g_col.spider_dir = -1;

    g_col.queen.x = Q8(158); g_col.queen.y = Q8(136);
    g_col.queen.face = 1;
    g_col.queen.state = Q_IDLE;
    g_col.queen.node = g_col.queen.node_to = 4;
    g_col.queen.hunger = 74;
    g_col.queen.energy = 88;
    g_col.queen.mood = 70;
    g_col.queen.hygiene = 84;
    g_col.queen.health = 100;
    g_col.queen.bond = 10;
    g_col.queen.wander_t = 40;

    static const uint8_t start_nodes[6] = { 5, 1, 2, 3, 4, 5 };
    for (int i = 0; i < 6; i++) {
        worker_t *w = &g_col.workers[i];
        w->alive = 1;
        w->name_id = (uint8_t)(i + 1);
        w->node = w->node_to = start_nodes[i];
        w->x = Q8(g_node_x[start_nodes[i]] + rnd_range(-10, 10));
        w->y = Q8(g_node_y[start_nodes[i]] + rnd_range(-5, 5));
        w->state = W_IDLE;
        w->energy = (uint8_t)rnd_range(70, 100);
        w->hunger = (uint8_t)rnd_range(10, 40);
        w->health = 100;
        w->face = chance(50) ? 1 : -1;
        w->timer = (uint16_t)rnd_range(20, 120);
    }
    g_col.n_workers = 6;

    colony_msg("Bem-vindo ao formigueiro!");
    colony_event("Colonia fundada. Cuide da rainha!");
    ESP_LOGI(TAG, "nova colonia: %u operarias, %u camaras, %u recursos",
             g_col.n_workers, g_col.n_chambers, g_col.n_res);
}

/* ============================ save / load ================================= */
size_t colony_serialize(uint8_t *buf, size_t cap)
{
    if (cap < sizeof(colony_t)) return 0;
    memcpy(buf, &g_col, sizeof(colony_t));
    colony_t *s = (colony_t *)buf;
    s->n_parts = 0;
    s->flash_ticks = 0;
    s->msg_ticks = 0;
    s->autosave_ticks = 0;
    return sizeof(colony_t);
}

bool colony_load(const void *buf, size_t len)
{
    build_dist();
    if (!buf || len != sizeof(colony_t)) return false;
    memcpy(&g_col, buf, sizeof(colony_t));
    g_col.n_parts = 0;
    g_col.msg_ticks = 0;
    g_col.autosave_ticks = 0;
    g_col.flash_ticks = 0;
    if (g_col.n_workers > COLONY_MAX_WORKERS) g_col.n_workers = COLONY_MAX_WORKERS;
    if (g_col.n_chambers > COLONY_MAX_CHAMBERS) g_col.n_chambers = COLONY_MAX_CHAMBERS;
    if (g_col.n_res > COLONY_MAX_RES) g_col.n_res = COLONY_MAX_RES;
    if (g_col.sec_of_day >= 86400) g_col.sec_of_day = 8 * 3600;
    if (!g_col.rng) g_col.rng = 0x12345678u;
    if (!g_col.day) g_col.day = 1;
    g_col.speed = 1;                       /* nunca volta pausado */
    g_col.cam_x = (int16_t)clampi(g_col.cam_x, 0, WORLD_W - VIEW_W);
    for (int i = 0; i < g_col.n_res; i++) g_col.res[i].claimed = 0;
    return true;
}

void colony_save_now(void)
{
    static uint8_t buf[sizeof(colony_t)];
    g_col.epoch_save = (int64_t)(esp_timer_get_time() / 1000000);
    size_t n = colony_serialize(buf, sizeof(buf));
    if (!n) return;
    esp_err_t e = game_save_write(buf, n);
    if (e != ESP_OK) ESP_LOGW(TAG, "falha ao gravar save: %s", esp_err_to_name(e));
    g_col.last_save_ms = esp_timer_get_time() / 1000;
}

/* ============================ movimento =================================== */
/* Anda em direção a (tx,ty) em px. `spd` = px por tick. Retorna true ao chegar. */
static bool walk_to(int32_t *x, int32_t *y, int tx, int ty, int spd, int8_t *face)
{
    int32_t cx = *x, cy = *y;
    int32_t gx = Q8(tx), gy = Q8(ty);
    int dx = gx - cx, dy = gy - cy;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int s = Q8(spd);
    if (adx <= s && ady <= s) { *x = gx; *y = gy; return true; }

    int steps = adx > ady ? adx : ady;
    int32_t sx = (int32_t)(((int64_t)dx * s) / steps);
    int32_t sy = (int32_t)(((int64_t)dy * s) / steps);
    if (!sx && adx) sx = dx > 0 ? Q8(1) / 2 : -Q8(1) / 2;
    if (!sy && ady) sy = dy > 0 ? Q8(1) / 2 : -Q8(1) / 2;
    *x = cx + sx;
    *y = cy + sy;
    if (face && sx) *face = (int8_t)(sx > 0 ? 1 : -1);
    return false;
}

static void set_path(worker_t *w, uint8_t to)
{
    w->node_to = to;
    w->path_len = (uint8_t)colony_find_path(w->node, to, w->path, COLONY_MAX_NODES);
    w->path_i = 0;
    w->state = W_GOTO;
    w->timer = 0;
}

/* segue o caminho; true quando chegou ao último nó */
static bool follow_path(int32_t *x, int32_t *y, uint8_t *node, int8_t *path,
                        uint8_t *path_len, uint8_t *path_i, uint8_t node_to,
                        int spd, int8_t *face)
{
    if (*path_len == 0 || *path_i >= *path_len) { *node = node_to; return true; }
    uint8_t nxt = (uint8_t)path[*path_i];
    if (walk_to(x, y, g_node_x[nxt], g_node_y[nxt], spd, face)) {
        *node = nxt;
        (*path_i)++;
        if (*path_i >= *path_len) return true;
    }
    return false;
}

static bool worker_busy(const worker_t *w)
{
    return w->state != W_IDLE && w->state != W_SLEEP;
}

static int count_state(uint8_t st)
{
    int n = 0;
    for (int i = 0; i < g_col.n_workers; i++) {
        if (g_col.workers[i].alive && g_col.workers[i].state == st) n++;
    }
    return n;
}

/* recurso disponível mais próximo do monte (presa só se pedida) */
static int pick_resource(uint8_t want_kind)
{
    int best = -1, bd = 1 << 28;
    for (int i = 0; i < g_col.n_res; i++) {
        resource_t *r = &g_col.res[i];
        if (!r->amount || r->claimed) continue;
        if (want_kind) { if (r->kind != want_kind) continue; }
        else if (r->kind == RES_PREY) continue;
        int d = r->x + (int)colony_rand_range(60);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

const char *colony_worker_state_name(uint8_t st)
{
    switch (st) {
    case W_IDLE:       return "livre";
    case W_GOTO:       return "andando";
    case W_EXIT:       return "saindo";
    case W_GO_RES:     return "indo coletar";
    case W_COLLECT:    return "coletando";
    case W_HUNT:       return "cacando";
    case W_RETURN:     return "voltando";
    case W_ENTER:      return "entrando";
    case W_DEPOSIT:    return "entregando";
    case W_TEND:       return "cuidando dos ovos";
    case W_TEND_QUEEN: return "cuidando da rainha";
    case W_BUILD:      return "escavando";
    case W_CLEAN:      return "fazendo faxina";
    case W_EAT:        return "comendo";
    case W_SLEEP:      return "dormindo";
    case W_SCOUT:      return "explorando";
    case W_FLEE:       return "fugindo";
    case W_STUCK:      return "abrigada da chuva";
    default:           return "-";
    }
}

/* ============================ eventos por hora ============================ */
static void hatch_worker(void)
{
    colony_t *c = &g_col;
    if (c->n_workers >= COLONY_MAX_WORKERS || c->n_workers >= colony_worker_cap()) {
        colony_msg("Bercario no limite: amplie para nascerem mais!");
        return;
    }
    worker_t *w = &c->workers[c->n_workers];
    memset(w, 0, sizeof(*w));
    w->alive = 1;
    w->name_id = (uint8_t)(c->n_workers + 1);
    w->node = w->node_to = 3;
    w->x = Q8(g_node_x[3] + rnd_range(-8, 8));
    w->y = Q8(g_node_y[3] + rnd_range(-4, 4));
    w->state = W_IDLE;
    w->energy = 90; w->hunger = 25; w->health = 100;
    w->timer = 60;
    c->n_workers++;
    c->points = (uint16_t)clampi(c->points + 25, 0, 65000);
    c->flash_ticks = 30;
    colony_msg("Nasceu a formiga %d!", w->name_id);
    colony_fx_spark(FX(UNQ8(w->x), UNQ8(w->y)), COL_GOLD);
    c->queen.mood = (uint8_t)clampi(c->queen.mood + 6, 0, 100);
}

static void on_hour(void)
{
    colony_t *c = &g_col;
    queen_t *q = &c->queen;
    uint8_t h = colony_hour();

    /* ---- rainha ---- */
    bool sleeping = (q->state == Q_SLEEP);
    q->age_h++;
    q->hunger  = (uint8_t)clampi(q->hunger - (sleeping ? 2 : 4), 0, 100);
    q->energy  = (uint8_t)clampi(q->energy + (sleeping ? 14 : -5), 0, 100);
    q->mood    = (uint8_t)clampi(q->mood - 2 + (c->hygiene < 40 ? -2 : 0) +
                                 (q->bond > 60 ? 1 : 0), 0, 100);
    q->hygiene = (uint8_t)clampi(q->hygiene - 1, 0, 100);

    int hp = 0;
    if (q->hunger <= 5)       hp -= 4;
    else if (q->hunger < 25)  hp -= 1;
    if (q->mood <= 10)        hp -= 2;
    if (q->energy <= 5)       hp -= 1;
    if (q->hygiene < 20)      hp -= 1;
    if (!hp && q->hunger > 50 && q->mood > 50 && q->energy > 30) hp = 2;
    q->health = (uint8_t)clampi(q->health + hp, 0, 100);

    /* ---- consumo da colônia (a cada 2 h) ---- */
    if ((h % 2) == 0) {
        int need = (colony_population() + 2) / 3;
        if (c->food >= need) {
            c->food = (int16_t)(c->food - need);
        } else {
            c->food = 0;
            q->mood = (uint8_t)clampi(q->mood - 4, 0, 100);
            c->alarm = 1;
            if (chance(40)) colony_msg("Faltou comida na despensa!");
        }
    }

    /* ---- higiene ---- */
    c->hygiene = (uint8_t)clampi(c->hygiene - 1 + (count_state(W_CLEAN) ? 1 : 0), 0, 100);
    for (int i = 0; i < c->n_chambers; i++) {
        chamber_t *ch = &c->chambers[i];
        if (ch->kind != CH_TRASH && ch->dirty < 100 && chance(20)) ch->dirty++;
    }

    /* ---- jardim de fungos ---- */
    {
        int i = ch_index(CH_FUNGUS);
        if (i >= 0) {
            chamber_t *f = &c->chambers[i];
            if (f->water > 4) f->water = (uint8_t)(f->water - 4);
            int prod = 2 * f->level + (f->water > 30 ? 1 : 0) + (colony_raining() ? 1 : 0);
            if (c->weather == WX_SUN && f->water < 10) prod = 0;
            if (prod > 0 && chance(70)) {
                add_food(prod);
                if (chance(20)) colony_fx_spark(FX(g_node_x[2] + rnd_range(-14, 14),
                                                  g_node_y[2] + rnd_range(-6, 6)), COL_LEAF);
            }
        }
    }

    /* ---- ciclo de vida: ovo -> larva -> pupa -> operária ---- */
    {
        int ni = ch_index(CH_NURSERY);
        int rate = 1 + (ni >= 0 ? c->chambers[ni].level - 1 : 0) + (count_state(W_TEND) ? 1 : 0);
        if (c->eggs) {
            c->egg_progress = (uint16_t)clampi(c->egg_progress + 17 * rate, 0, 100);
            if (c->egg_progress >= 100) {
                c->egg_progress = 0; c->eggs--; c->larvae++;
                colony_event("Um ovo virou larva.");
            }
        } else if (c->larvae) {
            c->egg_progress = (uint16_t)clampi(c->egg_progress + 12 * rate, 0, 100);
            if (c->egg_progress >= 100) {
                c->egg_progress = 0; c->larvae--; c->pupae++;
                colony_event("Uma larva virou pupa.");
            }
        } else if (c->pupae) {
            c->egg_progress = (uint16_t)clampi(c->egg_progress + 8 * rate, 0, 100);
            if (c->egg_progress >= 100) {
                c->egg_progress = 0; c->pupae--;
                hatch_worker();
            }
        } else {
            c->egg_progress = 0;
        }
    }

    /* ---- postura automática ---- */
    if (!colony_is_night() && q->hunger >= 65 && q->mood >= 45 && q->energy >= 35 &&
        (c->eggs + c->larvae + c->pupae) < colony_brood_cap() &&
        q->state == Q_IDLE && (h % 5) == 0 && chance(45)) {
        q->last_action = CMD_LAY_EGG;
        q->node_to = 3;
        q->path_len = (uint8_t)colony_find_path(q->node, 3, q->path, COLONY_MAX_NODES);
        q->path_i = 0;
        q->state = Q_TO_NURSERY;
        queen_say(SAY_EGG[colony_rand_range(2)]);
    }

    /* ---- recursos externos ---- */
    for (int i = 0; i < c->n_res; i++) {
        resource_t *r = &c->res[i];
        if (r->amount || r->claimed) continue;
        if (r->timer_h) { r->timer_h--; continue; }
        uint8_t grow = (c->weather == WX_RAIN) ? 2 : 1;
        r->amount = (uint8_t)clampi(r->max_amount / 2 + grow, 1, r->max_amount);
        colony_event("Cresceu: %s.", colony_res_name(r->kind));
    }

    /* ---- chuva: umidade e risco de alagamento ---- */
    if (colony_raining()) {
        int i = ch_index(CH_FUNGUS);
        if (i >= 0) c->chambers[i].water = (uint8_t)clampi(c->chambers[i].water + 12, 0, 100);
        if (c->weather == WX_STORM && chance(7)) {
            int li = ch_index(CH_TRASH);
            int drain = (li >= 0) ? c->chambers[li].level : 0;
            if (drain >= 2) {
                colony_msg("A drenagem da lixeira segurou a tormenta.");
            } else {
                if (i >= 0 && c->chambers[i].level > 1) c->chambers[i].level--;
                c->hygiene = (uint8_t)clampi(c->hygiene - 12, 0, 100);
                q->mood = (uint8_t)clampi(q->mood - 8, 0, 100);
                c->alarm = 3;
                colony_msg("A tormenta alagou o jardim de fungos!");
            }
        }
    }

    /* ---- clima simulado quando offline ---- */
    if (!c->net_ok) {
        c->weather_age_min = (uint16_t)(c->weather_age_min + 60);
        if (c->weather_age_min >= 180) { c->weather_age_min = 0; weather_simulate(); }
    } else {
        c->weather_age_min = (uint16_t)(c->weather_age_min + 60);
    }

    /* ---- aranha (derrotável; volta depois de ~2 dias) ---- */
    if (c->spider_x < 0) {
        if (s_spider_back_at && c->total_ticks >= s_spider_back_at) {
            c->spider_x = 520; c->spider_y = 104; c->spider_dir = -1;
            s_spider_hp = 100; s_spider_back_at = 0;
            colony_msg("Uma aranha voltou a rondar o jardim...");
        }
    } else if (chance(30)) {
        c->spider_x = (int16_t)clampi(c->spider_x + c->spider_dir * rnd_range(2, 8), 470, 620);
        if (c->spider_x <= 470 || c->spider_x >= 620) c->spider_dir = (int8_t)-c->spider_dir;
    }

    /* ---- alarmes ---- */
    if (q->hunger < 20 || c->food == 0) c->alarm = 1;
    else if (c->weather == WX_STORM)    c->alarm = 3;
    else if (q->health < 40)            c->alarm = 2;
    else                                c->alarm = 0;

    c->points = (uint16_t)clampi(c->points + 1, 0, 65000);

    if (!q->say_timer && chance(12)) queen_say_auto();

    if (h == 0) {
        c->day++;
        c->points = (uint16_t)clampi(c->points + 20, 0, 65000);
        colony_event("Dia %lu comecou.", (unsigned long)c->day);
    }
}

/* ============================ IA das operárias ============================ */
static void worker_on_node(worker_t *w);

/* decide a próxima tarefa conforme a fase do dia / necessidades */
static bool worker_state_is_outside(uint8_t st);   /* fwd decl */

static int count_outside(void)
{
    int n = 0;
    for (int i = 0; i < g_col.n_workers; i++)
        if (g_col.workers[i].alive && worker_state_is_outside(g_col.workers[i].state)) n++;
    return n;
}

/* sair para o exterior respeitando o teto da equipe */
static bool try_leave(worker_t *w)
{
    if (count_outside() >= s_forage_cap) {
        set_path(w, colony_rand_range(2) ? 6 : 3);   /* equipe cheia: fica dentro */
        return false;
    }
    set_path(w, 0);
    w->flags = F_LEAVE;
    return true;
}

static void worker_decide(worker_t *w)
{
    colony_t *c = &g_col;
    colony_phase_t ph = colony_phase();

    w->flags = 0;

    if (w->energy < 20) { set_path(w, 3); w->flags = F_SLEEP; return; }
    if (w->hunger > 65) {
        if (c->food > 0) { set_path(w, 1); w->flags = F_EAT; return; }
    }
    if (ph == PHASE_NIGHT) { set_path(w, 3); w->flags = F_SLEEP; return; }

    int r = (int)colony_rand_range(100);

    if (colony_raining()) {                  /* chuva: só tarefas internas */
        if (r < 45)      { set_path(w, 3); }              /* berçário */
        else if (r < 80) { set_path(w, 6); }              /* faxina/obra */
        else             { set_path(w, 4); }              /* cuidar da rainha */
        return;
    }

    switch (ph) {
    case PHASE_DAWN:
        if (r < 35)      set_path(w, 4);
        else if (r < 60) set_path(w, 3);
        else             try_leave(w);
        break;
    case PHASE_DAY:
        if (r < 65)      try_leave(w);
        else if (r < 80) set_path(w, 6);
        else if (r < 92) set_path(w, 3);
        else             set_path(w, 4);
        break;
    case PHASE_AFTERNOON:
        if (r < 35)      try_leave(w);
        else if (r < 65) set_path(w, 6);
        else if (r < 85) set_path(w, 3);
        else             set_path(w, 4);
        break;
    case PHASE_EVENING:
        if (r < 40)      { set_path(w, 1); w->flags = F_EAT; }
        else if (r < 70) set_path(w, 4);
        else             set_path(w, 3);
        break;
    default:
        set_path(w, 5);
        break;
    }
}

/* chegou ao nó destino: executa a intenção */
static void worker_on_node(worker_t *w)
{
    colony_t *c = &g_col;

    /* carga nas costas -> sempre entrega na despensa */
    if (w->carry) {
        if (w->node == 1) {
            add_food(w->carry_food);
            add_material(w->carry_mat);
            c->points = (uint16_t)clampi(c->points + w->carry_food + w->carry_mat, 0, 65000);
            c->tasks_done++;
            if (w->carry == RES_FLOWER) c->queen.mood = (uint8_t)clampi(c->queen.mood + 3, 0, 100);
            colony_fx_text(FX(UNQ8(w->x), UNQ8(w->y) - 8), "+", COL_UI_OK);
            w->carry = 0; w->carry_food = 0; w->carry_mat = 0;
            w->state = W_IDLE;
            w->timer = 30;
        } else {
            set_path(w, 1);
            w->flags = 0;
        }
        return;
    }

    if (w->flags & F_SLEEP) { w->flags &= (uint8_t)~F_SLEEP; w->state = W_SLEEP; w->timer = (uint16_t)rnd_range(200, 700); return; }
    if (w->flags & F_EAT) {
        w->flags &= (uint8_t)~F_EAT;
        if (c->food > 0) { c->food--; w->state = W_EAT; w->timer = 40; }
        else { w->state = W_IDLE; w->timer = 40; }
        return;
    }
    if (w->flags & F_BUILD) { w->flags &= (uint8_t)~F_BUILD; w->state = W_BUILD; w->timer = 90; return; }
    if (w->flags & F_LEAVE) { w->flags &= (uint8_t)~F_LEAVE; w->state = W_EXIT; return; }

    switch (w->node_to) {
    case 2:                                     /* jardim de fungos: umedecer */
        w->state = W_TEND; w->timer = (uint16_t)rnd_range(100, 200);
        break;
    case 3:                                     /* berçário */
        if (c->eggs + c->larvae + c->pupae > 0) { w->state = W_TEND; w->timer = (uint16_t)rnd_range(120, 260); }
        else { w->state = W_IDLE; w->timer = 60; }
        break;
    case 4:                                     /* câmara real */
        w->state = W_TEND_QUEEN;
        w->timer = (uint16_t)rnd_range(80, 160);
        break;
    case 6:                                     /* lixeira: faxina */
        w->state = W_CLEAN; w->timer = (uint16_t)rnd_range(80, 160);
        break;
    default:
        w->state = W_IDLE;
        w->timer = (uint16_t)rnd_range(40, 140);
        break;
    }
}

static void worker_die(worker_t *w, const char *why)
{
    colony_msg("Formiga %d %s", w->name_id, why);
    g_col.queen.mood = (uint8_t)clampi(g_col.queen.mood - 15, 0, 100);
    g_col.queen.health = (uint8_t)clampi(g_col.queen.health - 4, 0, 100);
    g_col.hygiene = (uint8_t)clampi(g_col.hygiene - 5, 0, 100);
    if (w->res_target) {
        int ri = w->res_target - 1;
        if (ri < g_col.n_res) g_col.res[ri].claimed = 0;
        w->res_target = 0;
    }
    w->alive = 0;
}

static void worker_update(worker_t *w, int idx)
{
    colony_t *c = &g_col;

    /* relógio vital escalonado */
    if ((c->total_ticks % 20) == (uint32_t)(idx % 20)) {
        if (w->state == W_SLEEP) {
            w->energy = (uint8_t)clampi(w->energy + 5, 0, 100);
            w->hunger = (uint8_t)clampi(w->hunger + 1, 0, 100);
        } else {
            w->energy = (uint8_t)clampi(w->energy - 1, 0, 100);
            w->hunger = (uint8_t)clampi(w->hunger + 1, 0, 100);
        }
        if (w->health < 100 && w->energy > 40 && w->hunger < 60) w->health++;
        if (w->health <= 0) { worker_die(w, "nao resistiu..."); return; }
    }

    /* contra-ataque: operárias perto da aranha revidam (ela TEM vida) */
    if (c->spider_x >= 0 && (c->total_ticks % 10) == (uint32_t)(idx % 10) &&
        (w->state == W_HUNT || w->state == W_GO_RES || w->state == W_SCOUT ||
         w->state == W_FLEE)) {
        int dx = c->spider_x - UNQ8(w->x), dy = c->spider_y - UNQ8(w->y);
        if (dx * dx + dy * dy < 30 * 30) {
            s_spider_hp = (uint8_t)(s_spider_hp > 3 ? s_spider_hp - 3 : 0);
            if ((c->total_ticks % 30) == 0)
                colony_fx_spark(FX(c->spider_x - c->cam_x, c->spider_y - 8), COL_UI_WARN);
            if (!s_spider_hp) {
                c->spider_x = -1;
                s_spider_back_at = c->total_ticks + 2u * 96000u;   /* ~2 dias de jogo */
                add_food(25);
                c->points = (uint16_t)clampi(c->points + 60, 0, 65000);
                c->flash_ticks = 30;
                c->alarm = 0;
                colony_msg("As formigas venceram a aranha! +25 comida.");
            }
        }
    }

    switch (w->state) {
    case W_IDLE:
        if (w->timer) { w->timer--; break; }
        if (colony_is_night()) { set_path(w, 3); w->flags = F_SLEEP; break; }
        if (chance(30)) {                        /* perambula na câmara */
            w->tx = (int16_t)(g_node_x[w->node] + rnd_range(-14, 14));
            w->ty = (int16_t)(g_node_y[w->node] + rnd_range(-7, 7));
            w->flags |= F_FREE;
            w->state = W_GOTO;
        } else {
            worker_decide(w);
        }
        break;

    case W_SLEEP:
        if (w->timer) w->timer--;
        if ((c->total_ticks % 70) == (uint32_t)(idx % 70)) colony_fx_zzz(FX(UNQ8(w->x), UNQ8(w->y) - 8));
        if (!w->timer || (!colony_is_night() && w->energy > 75)) {
            w->state = W_IDLE; w->timer = (uint16_t)rnd_range(20, 60);
        }
        break;

    case W_EAT:
        if (w->timer) w->timer--;
        else { w->hunger = 0; w->energy = (uint8_t)clampi(w->energy + 15, 0, 100);
               w->state = W_IDLE; w->timer = 40; }
        break;

    case W_GOTO:
        if (w->flags & F_FREE) {
            if (walk_to(&w->x, &w->y, w->tx, w->ty, 1, &w->face)) {
                w->flags &= (uint8_t)~F_FREE;
                w->state = W_IDLE;
                w->timer = (uint16_t)rnd_range(30, 100);
            }
        } else if (follow_path(&w->x, &w->y, &w->node, w->path, &w->path_len,
                               &w->path_i, w->node_to, 1, &w->face)) {
            worker_on_node(w);
        }
        break;

    case W_TEND:
    case W_TEND_QUEEN:
    case W_CLEAN:
        if ((c->total_ticks % 12) == 0) w->x += rnd_range(-Q8(2), Q8(2));
        if (w->state == W_TEND_QUEEN && (c->total_ticks % 30) == 0) {
            colony_fx_spark(FX(UNQ8(c->queen.x), UNQ8(c->queen.y) - 6), COL_UI_INFO);
        }
        if (w->timer) w->timer--;
        else {
            if (w->state == W_TEND_QUEEN) {
                c->queen.mood = (uint8_t)clampi(c->queen.mood + 3, 0, 100);
                c->queen.hygiene = (uint8_t)clampi(c->queen.hygiene + 5, 0, 100);
                if (c->queen.hunger < 70 && c->food >= 2) {
                    c->food = (int16_t)(c->food - 2);
                    c->queen.hunger = (uint8_t)clampi(c->queen.hunger + 8, 0, 100);
                }
            } else if (w->state == W_CLEAN) {
                c->hygiene = (uint8_t)clampi(c->hygiene + 6, 0, 100);
            } else if (w->node_to == 2) {
                int fi = ch_index(CH_FUNGUS);
                if (fi >= 0)
                    c->chambers[fi].water = (uint8_t)clampi(c->chambers[fi].water + 10, 0, 100);
                if (chance(25)) colony_msg("Operaria umedeceu o jardim de fungos.");
            }
            c->tasks_done++;
            w->state = W_IDLE;
            w->timer = (uint16_t)rnd_range(40, 120);
        }
        break;

    case W_BUILD:
        if ((c->total_ticks % 8) == 0) colony_fx_dirt(FX(UNQ8(w->x) + rnd_range(-4, 4), UNQ8(w->y) + 4));
        if (w->timer) w->timer--;
        else {
            int ci = s_build_ci;
            if (ci >= 0 && ci < c->n_chambers && colony_build_cost(ci)) {
                c->chambers[ci].level++;
                c->chambers[ci].dirty = 0;
                c->points = (uint16_t)clampi(c->points + 40, 0, 65000);
                c->flash_ticks = 30;
                c->tasks_done++;
                colony_msg("%s ampliada para nivel %d!",
                           colony_chamber_name(ci), c->chambers[ci].level);
                colony_event("Obra concluida. Novos limites ativos.");
            }
            s_build_ci = -1;      /* 1ª operária a concluir fecha a obra */
            w->state = W_IDLE;
            w->timer = 30;
        }
        break;

    case W_EXIT:
        /* sobe até a superfície pelo túnel de entrada */
        if (w->node != 0) {
            if (w->path_len == 0 || w->path_i >= w->path_len) set_path(w, 0);
            if (follow_path(&w->x, &w->y, &w->node, w->path, &w->path_len,
                            &w->path_i, 0, 1, &w->face)) {
                /* chegou na boca do túnel */
            } else break;
        }
        if (walk_to(&w->x, &w->y, 160, 6, 1, &w->face)) {
            w->node = 7;
            w->x = Q8(32); w->y = Q8(112);          /* mundo exterior */
            if (w->res_target) {
                w->state = W_GO_RES;
            } else if (c->marker_on) {
                w->state = W_SCOUT;
                w->tx = c->marker_x; w->ty = c->marker_y;
            } else {
                int ri = pick_resource(0);
                if (ri < 0) { w->state = W_RETURN; break; }
                w->res_target = (uint8_t)(ri + 1);
                c->res[ri].claimed = (uint8_t)(idx + 1);
                w->state = W_GO_RES;
            }
        }
        break;

    case W_GO_RES: {
        if (colony_raining()) { w->state = W_STUCK; w->timer = 60; break; }
        int ri = w->res_target ? w->res_target - 1 : -1;
        if (ri < 0 || ri >= c->n_res || !c->res[ri].amount) {
            if (ri >= 0 && ri < c->n_res) c->res[ri].claimed = 0;
            ri = pick_resource(w->res_target ? c->res[w->res_target - 1].kind : 0);
            if (ri < 0) { w->res_target = 0; w->state = W_RETURN; break; }
            w->res_target = (uint8_t)(ri + 1);
            c->res[ri].claimed = (uint8_t)(idx + 1);
        }
        w->tx = c->res[ri].x; w->ty = c->res[ri].y;

        int dx = c->spider_x - UNQ8(w->x), dy = c->spider_y - UNQ8(w->y);
        if (dx * dx + dy * dy < 38 * 38 && !w->hurt && chance(20)) {
            w->health = (uint8_t)clampi(w->health - 25, 0, 100);
            w->hurt = 1;
            c->alarm = 2;
            c->queen.mood = (uint8_t)clampi(c->queen.mood - 6, 0, 100);
            colony_msg("Uma aranha atacou a formiga %d!", w->name_id);
            colony_fx_text(FX(UNQ8(w->x), UNQ8(w->y) - 10), "ai!", COL_UI_BAD);
            c->res[ri].claimed = 0;
            w->res_target = 0;
            w->state = W_FLEE;
            break;
        }

        if (walk_to(&w->x, &w->y, w->tx, w->ty, 1, &w->face)) {
            uint8_t f, m, t;
            colony_res_yield(c->res[ri].kind, &f, &m, &t);
            w->carry = c->res[ri].kind;
            w->carry_food = f; w->carry_mat = m;
            w->timer = t;
            c->res[ri].amount--;
            if (!c->res[ri].amount) c->res[ri].timer_h = c->res[ri].respawn_h;
            c->res[ri].claimed = 0;
            w->res_target = 0;
            /* presa = caça (luta); o resto = coleta simples */
            w->state = (w->carry == RES_PREY) ? W_HUNT : W_COLLECT;
            if (w->state == W_HUNT) {
                w->timer = (uint16_t)(t + 60);
                colony_msg("As formigas cercaram a presa!");
                c->alarm = 2;
            }
        }
        break;
    }

    case W_COLLECT:
        if ((c->total_ticks % 12) == 0) colony_fx_spark(FX(UNQ8(w->x), UNQ8(w->y) - 4), COL_LEAF);
        if (w->timer) w->timer--;
        else { w->state = W_RETURN; colony_fx_text(FX(UNQ8(w->x), UNQ8(w->y) - 10), "+", COL_UI_OK); }
        break;

    case W_HUNT:
        if (w->timer) {
            w->timer--;
            if ((c->total_ticks % 12) == 0) colony_fx_spark(FX(UNQ8(w->x), UNQ8(w->y) - 4), COL_UI_WARN);
        } else {
            w->state = W_RETURN;
        }
        break;

    case W_STUCK:
        if (w->timer) w->timer--;
        else if (!colony_raining()) w->state = W_GO_RES;
        else w->state = W_RETURN;
        break;

    case W_SCOUT:
        if (walk_to(&w->x, &w->y, w->tx, w->ty, 1, &w->face)) {
            c->marker_on = 0;
            colony_event("Batedora chegou ao destino.");
            w->state = W_RETURN;
        }
        break;

    case W_FLEE:
    case W_RETURN:
        if (walk_to(&w->x, &w->y, 32, 112, w->carry ? 1 : 2, &w->face)) {
            w->state = W_ENTER;
            w->x = Q8(160); w->y = Q8(6);
            w->node = 0;
        }
        break;

    case W_ENTER:
        if (walk_to(&w->x, &w->y, g_node_x[0], g_node_y[0], 1, &w->face)) {
            w->node = 0;
            set_path(w, w->carry ? 1 : 5);
            w->flags = 0;
        }
        break;

    default:
        w->state = W_IDLE;
        w->timer = 30;
        break;
    }
}

static bool worker_state_is_outside(uint8_t st)
{
    return st == W_GO_RES || st == W_COLLECT || st == W_HUNT || st == W_RETURN ||
           st == W_SCOUT || st == W_STUCK || st == W_FLEE || st == W_EXIT;
}

static void workers_compact(void)
{
    int n = 0;
    for (int i = 0; i < g_col.n_workers; i++) {
        if (g_col.workers[i].alive) {
            if (n != i) g_col.workers[n] = g_col.workers[i];
            n++;
        }
    }
    for (int i = n; i < g_col.n_workers; i++) memset(&g_col.workers[i], 0, sizeof(worker_t));
    if (n != g_col.n_workers) g_col.sel_worker = 0;
    g_col.n_workers = (uint8_t)n;
}

/* ============================ IA da rainha ================================ */
static void queen_set_path(uint8_t to)
{
    queen_t *q = &g_col.queen;
    q->node_to = to;
    q->path_len = (uint8_t)colony_find_path(q->node, to, q->path, COLONY_MAX_NODES);
    q->path_i = 0;
    q->state = Q_WALK;
}

static void queen_arrive(void)
{
    colony_t *c = &g_col;
    queen_t *q = &c->queen;

    switch (q->last_action) {
    case CMD_FEED:
        if (c->food >= 8) {
            c->food = (int16_t)(c->food - 8);
            q->hunger = (uint8_t)clampi(q->hunger + 30, 0, 100);
            q->mood = (uint8_t)clampi(q->mood + 5, 0, 100);
            q->energy = (uint8_t)clampi(q->energy + 5, 0, 100);
            q->state = Q_EAT; q->timer = 120;
            colony_fx_text(FX(UNQ8(q->x), UNQ8(q->y) - 14), "nhac", COL_HONEY);
            queen_say("Hmmm, que delicia!");
            c->points = (uint16_t)clampi(c->points + 5, 0, 65000);
        } else {
            colony_msg("Sem comida na despensa!");
            q->state = Q_IDLE; q->wander_t = 40;
        }
        break;
    case CMD_PLAY:
        q->energy = (uint8_t)clampi(q->energy - 10, 0, 100);
        q->mood = (uint8_t)clampi(q->mood + 14, 0, 100);
        q->bond = (uint8_t)clampi(q->bond + 4, 0, 100);
        q->state = Q_PLAY; q->timer = 160;
        queen_say("Adoro brincar com voce!");
        c->points = (uint16_t)clampi(c->points + 8, 0, 65000);
        break;
    case CMD_GROOM:
        q->hygiene = 100;
        q->mood = (uint8_t)clampi(q->mood + 8, 0, 100);
        c->hygiene = (uint8_t)clampi(c->hygiene + 8, 0, 100);
        q->state = Q_GROOM; q->timer = 120;
        queen_say("Que banho bom!");
        c->points = (uint16_t)clampi(c->points + 4, 0, 65000);
        break;
    case CMD_REST:
        q->state = Q_SLEEP; q->timer = 600;
        queen_say(SAY_TIRED[colony_rand_range(3)]);
        break;
    case CMD_LAY_EGG:
        q->state = Q_LAY; q->timer = 140;
        break;
    default:
        q->state = Q_IDLE; q->wander_t = 40;
        break;
    }
    q->last_action = CMD_NONE;
}

static void queen_update(void)
{
    colony_t *c = &g_col;
    queen_t *q = &c->queen;

    if (q->say_timer) q->say_timer--;
    if (q->pet_cd) q->pet_cd--;

    switch (q->state) {
    case Q_SLEEP:
        if ((c->total_ticks % 70) == 0) colony_fx_zzz(FX(UNQ8(q->x) + 8, UNQ8(q->y) - 12));
        if (q->timer) q->timer--;
        if (!q->timer || (!colony_is_night() && q->energy > 80)) {
            q->state = Q_IDLE; q->wander_t = 40;
        }
        break;

    case Q_WALK:
        if (q->path_len == 0) {                /* passeio livre na câmara */
            if (walk_to(&q->x, &q->y, q->tx, q->ty, 1, &q->face)) {
                q->state = Q_IDLE;
                q->wander_t = (uint16_t)rnd_range(80, 220);
            }
        } else if (follow_path(&q->x, &q->y, &q->node, q->path, &q->path_len,
                               &q->path_i, q->node_to, 1, &q->face)) {
            queen_arrive();
        }
        break;

    case Q_TO_NURSERY:
        if (follow_path(&q->x, &q->y, &q->node, q->path, &q->path_len, &q->path_i,
                        3, 1, &q->face)) {
            q->state = Q_LAY; q->timer = 140;
        }
        break;

    case Q_EAT:
    case Q_PLAY:
    case Q_GROOM:
    case Q_PET:
        if (q->timer) q->timer--;
        if (q->state == Q_PLAY && (c->total_ticks % 14) == 0) {
            colony_fx_spark(FX(UNQ8(q->x) + rnd_range(-12, 12), UNQ8(q->y) + rnd_range(-10, 4)), COL_GOLD);
        }
        if (q->state == Q_GROOM && (c->total_ticks % 10) == 0) {
            colony_fx_spark(FX(UNQ8(q->x) + rnd_range(-8, 8), UNQ8(q->y)), COL_UI_INFO);
        }
        if (q->state == Q_PET && (c->total_ticks % 12) == 0) {
            colony_fx_heart(FX(UNQ8(q->x) + rnd_range(-6, 12), UNQ8(q->y) - 14));
        }
        if (!q->timer) {
            if (q->node != 4) { queen_set_path(4); q->last_action = CMD_NONE; }
            else { q->state = Q_IDLE; q->wander_t = 40; }
        }
        break;

    case Q_LAY:
        if (q->timer) q->timer--;
        else {
            if (q->hunger >= 55 && q->mood >= 35 && q->energy >= 25 &&
                (c->eggs + c->larvae + c->pupae) < colony_brood_cap()) {
                c->eggs++;
                q->eggs_laid++;
                q->hunger = (uint8_t)clampi(q->hunger - 14, 0, 100);
                q->energy = (uint8_t)clampi(q->energy - 12, 0, 100);
                q->mood = (uint8_t)clampi(q->mood + 8, 0, 100);
                c->points = (uint16_t)clampi(c->points + 15, 0, 65000);
                c->flash_ticks = 24;
                colony_msg("A rainha pos um ovo!");
                colony_fx_text(FX(UNQ8(q->x), UNQ8(q->y) - 14), "ovo!", COL_EGG);
                colony_fx_spark(FX(UNQ8(q->x), UNQ8(q->y)), COL_GOLD);
            } else {
                colony_msg("A rainha nao esta em condicoes de por ovos.");
            }
            if (q->node != 4) queen_set_path(4);
            else { q->state = Q_IDLE; q->wander_t = 40; }
        }
        break;

    case Q_HUNTING:
        if (q->timer) q->timer--;
        else { q->state = Q_IDLE; q->wander_t = 40; }
        break;

    case Q_SAD:
        if (q->timer) q->timer--;
        else { q->state = Q_IDLE; q->wander_t = 40; }
        break;

    case Q_IDLE:
    default:
        if (colony_is_night()) {
            if (q->node != 4) { queen_set_path(4); break; }
            q->state = Q_SLEEP; q->timer = 800;
            break;
        }
        if (q->energy < 12 && q->state == Q_IDLE) {
            q->state = Q_SLEEP; q->timer = 400;
            queen_say(SAY_TIRED[colony_rand_range(3)]);
            break;
        }
        if (q->wander_t) { q->wander_t--; break; }
        q->wander_t = (uint16_t)rnd_range(80, 220);
        q->tx = (int16_t)rnd_range(126, 200);
        q->ty = (int16_t)rnd_range(126, 150);
        q->path_len = 0;
        q->last_action = CMD_NONE;
        q->state = Q_WALK;
        break;
    }
}

/* ============================ ações do usuário ============================ */
static void do_pet(void)
{
    queen_t *q = &g_col.queen;
    if (q->state == Q_SLEEP) { colony_msg("Shhh... a rainha esta dormindo."); return; }
    if (q->pet_cd) { colony_msg("Deixa ela respirar um pouco!"); return; }
    q->pet_cd = 160;
    q->mood = (uint8_t)clampi(q->mood + 8, 0, 100);
    q->bond = (uint8_t)clampi(q->bond + 3, 0, 100);
    q->state = Q_PET;
    q->timer = 80;
    g_col.points = (uint16_t)clampi(g_col.points + 2, 0, 65000);
    g_col.flash_ticks = 12;
    for (int i = 0; i < 3; i++) {
        colony_fx_heart(FX(UNQ8(q->x) + rnd_range(-8, 12), UNQ8(q->y) - 12 - i * 3));
    }
    queen_say(SAY_PET[colony_rand_range(4)]);
}

static void do_feed(void)
{
    colony_t *c = &g_col;
    queen_t *q = &c->queen;
    if (c->food < 8) { colony_msg("Despensa quase vazia: mande coletar!"); return; }
    if (q->hunger > 96) { colony_msg("A rainha nao esta com fome."); return; }
    q->last_action = CMD_FEED;
    queen_set_path(1);
    colony_msg("A rainha foi ate a despensa comer.");
}

static void do_play(void)
{
    queen_t *q = &g_col.queen;
    if (q->energy < 15) { colony_msg("A rainha esta exausta: deixe-a descansar."); return; }
    if (colony_is_night()) { colony_msg("A noite ela prefere dormir."); return; }
    q->last_action = CMD_PLAY;
    queen_set_path(5);
    colony_msg("Hora de brincar no atrio!");
}

static void do_groom(void)
{
    g_col.queen.last_action = CMD_GROOM;
    queen_set_path(4);
    colony_msg("Banho real em andamento...");
}

static void do_rest(void)
{
    g_col.queen.last_action = CMD_REST;
    queen_set_path(4);
    colony_msg("A rainha vai descansar.");
}

static void do_hunt(void)
{
    colony_t *c = &g_col;
    queen_t *q = &c->queen;
    if (q->energy < 35) { colony_msg("A rainha precisa de energia para cacar."); return; }
    if (colony_raining()) { colony_msg("Chovendo: cacada cancelada."); return; }
    if (colony_is_night()) { colony_msg("As presas se escondem a noite."); return; }

    int prey = pick_resource(RES_PREY);
    if (prey < 0) { colony_msg("Nenhuma presa por perto agora."); return; }

    int sent = 0;
    for (int i = 0; i < c->n_workers && sent < 3; i++) {
        worker_t *w = &c->workers[i];
        if (!w->alive || worker_busy(w)) continue;
        w->flags = 0;
        w->res_target = (uint8_t)(prey + 1);
        c->res[prey].claimed = (uint8_t)(i + 1);
        set_path(w, 0);
        w->flags = F_LEAVE;
        sent++;
    }
    if (!sent) { colony_msg("Nenhuma operaria livre para a cacada."); return; }

    q->energy = (uint8_t)clampi(q->energy - 12, 0, 100);
    q->state = Q_HUNTING;
    q->timer = 240;
    queen_say("Ao ataque, minhas formigas!");
    colony_msg("Expedicao de cacada enviada!");
    c->points = (uint16_t)clampi(c->points + 5, 0, 65000);
}

static uint8_t ch_node(uint8_t kind)
{
    switch (kind) {
    case CH_STORAGE: return 1;
    case CH_FUNGUS:  return 2;
    case CH_NURSERY: return 3;
    case CH_ROYAL:   return 4;
    case CH_TRASH:   return 6;
    default:         return 5;      /* CH_ATRIUM */
    }
}

static void do_build(void)
{
    colony_t *c = &g_col;
    int ci = c->sel_chamber ? c->sel_chamber - 1 : -1;
    if (ci < 0) { colony_msg("Toque numa camara primeiro para ampliar."); return; }
    if (s_build_ci >= 0) {
        colony_msg("Obra ja em andamento no(a) %s.", colony_chamber_name((int)s_build_ci));
        return;
    }
    uint8_t cost = colony_build_cost(ci);
    if (!cost) { colony_msg("%s ja esta no nivel maximo.", colony_chamber_name(ci)); return; }
    if (c->material < cost) {
        colony_msg("Falta material: custo %u, tem %d. Colete gravetos/pedras la fora.",
                   cost, c->material);
        return;
    }
    int sent = 0;
    for (int i = 0; i < c->n_workers && sent < 2; i++) {
        worker_t *w = &c->workers[i];
        if (!w->alive || worker_busy(w)) continue;
        w->flags = F_BUILD;
        set_path(w, ch_node(c->chambers[ci].kind));
        sent++;
    }
    if (!sent) { colony_msg("Sem operarias livres para a obra."); return; }
    c->material = (int16_t)(c->material - cost);
    s_build_ci = (int8_t)ci;
    colony_msg("Obra no(a) %s: %d operarias a caminho (-%u material).",
               colony_chamber_name(ci), sent, cost);
}

static void do_tend(void)
{
    colony_t *c = &g_col;
    if (!c->eggs && !c->larvae && !c->pupae) { colony_msg("Nada para cuidar no bercario."); return; }
    int sent = 0;
    for (int i = 0; i < c->n_workers && sent < 2; i++) {
        worker_t *w = &c->workers[i];
        if (!w->alive || worker_busy(w)) continue;
        w->flags = 0;
        set_path(w, 3);
        sent++;
    }
    colony_msg(sent ? "Operarias cuidando do bercario." : "Sem operarias livres.");
}

static void do_forage(void)
{
    colony_t *c = &g_col;
    if (colony_raining()) { colony_msg("Chovendo: ninguem sai da colonia."); return; }
    if (colony_is_night()) { colony_msg("A noite a colonia fica selada."); return; }
    int sent = 0;
    for (int i = 0; i < c->n_workers && sent < 4; i++) {
        worker_t *w = &c->workers[i];
        if (!w->alive || worker_busy(w)) continue;
        w->flags = F_LEAVE;
        w->res_target = 0;
        set_path(w, 0);
        w->flags |= F_LEAVE;
        sent++;
    }
    colony_msg(sent ? "%d operarias sairam para coletar." : "Sem operarias livres.");
}

static void do_lay(void)
{
    colony_t *c = &g_col;
    queen_t *q = &c->queen;
    if ((c->eggs + c->larvae + c->pupae) >= colony_brood_cap()) {
        colony_msg("Bercario cheio! Amplie o bercario.");
        return;
    }
    if (q->hunger < 55) { colony_msg("A rainha precisa comer antes de por ovos."); return; }
    if (q->energy < 25) { colony_msg("A rainha esta cansada demais."); return; }
    q->last_action = CMD_LAY_EGG;
    queen_set_path(3);
    colony_msg("A rainha foi ao bercario por ovos.");
}

static void do_clean(void)
{
    colony_t *c = &g_col;
    int sent = 0;
    for (int i = 0; i < c->n_workers && sent < 2; i++) {
        worker_t *w = &c->workers[i];
        if (!w->alive || worker_busy(w)) continue;
        w->flags = 0;
        set_path(w, 6);
        sent++;
    }
    colony_msg(sent ? "Faxina em andamento." : "Sem operarias livres.");
}

void colony_command(const app_cmd_t *cmd)
{
    colony_t *c = &g_col;
    switch (cmd->id) {
    case CMD_PET:     do_pet(); break;
    case CMD_FEED:    do_feed(); break;
    case CMD_PLAY:    do_play(); break;
    case CMD_GROOM:   do_groom(); break;
    case CMD_REST:    do_rest(); break;
    case CMD_HUNT:    do_hunt(); break;
    case CMD_BUILD:   do_build(); break;
    case CMD_TEND:    do_tend(); break;
    case CMD_FORAGE:  do_forage(); break;
    case CMD_LAY_EGG: do_lay(); break;

    case CMD_TAP_CHAMBER: {
        int i = cmd->val;
        if (i >= 0 && i < c->n_chambers) {
            c->sel_chamber = (uint8_t)(i + 1);
            c->sel_worker = 0;
            chamber_t *ch = &c->chambers[i];
            uint8_t cost = colony_build_cost(i);
            const char *mx = cost ? "" : " (max)";
            switch (ch->kind) {
            case CH_STORAGE:
                colony_msg("Despensa nv.%d%s: %d comida, %d material.",
                           ch->level, mx, c->food, c->material);
                break;
            case CH_FUNGUS:
                colony_msg("Jardim nv.%d%s: umid. %d%%, rende ~%d comida/2h.", ch->level, mx,
                           ch->water, 2 * ch->level + (ch->water > 30 ? 1 : 0) +
                           (colony_raining() ? 1 : 0));
                break;
            case CH_NURSERY:
                colony_msg("Bercario nv.%d%s: %d ovos, %d larvas, %d pupas (cap %d).",
                           ch->level, mx, c->eggs, c->larvae, c->pupae, colony_brood_cap());
                break;
            case CH_ROYAL:
                colony_msg("Camara real nv.%d%s: humor %d%%, energia %d%%.",
                           ch->level, mx, c->queen.mood, c->queen.energy);
                break;
            case CH_TRASH:
                colony_msg("Lixeira nv.%d%s: higiene %d%%. Faxina despachada.",
                           ch->level, mx, c->hygiene);
                do_clean();
                break;
            default:
                colony_msg("%s nv.%d%s", colony_chamber_name(i), ch->level, mx);
                break;
            }
        }
        break;
    }
    case CMD_TAP_WORKER: {
        int i = cmd->val;
        if (i >= 0 && i < c->n_workers) {
            worker_t *w = &c->workers[i];
            c->sel_worker = (uint8_t)(i + 1);
            c->sel_chamber = 0;
            colony_msg("Formiga %d: %s (energia %d%%)", w->name_id,
                       colony_worker_state_name(w->state), w->energy);
        }
        break;
    }
    case CMD_TAP_RESOURCE: {
        int i = cmd->val;
        if (i < 0 || i >= c->n_res || !c->res[i].amount) break;
        if (colony_raining()) { colony_msg("Chovendo: coleta suspensa."); break; }
        if (colony_is_night()) { colony_msg("A noite a colonia fica selada."); break; }
        int wi = -1;
        for (int k = 0; k < c->n_workers; k++) {
            if (c->workers[k].alive && !worker_busy(&c->workers[k])) { wi = k; break; }
        }
        if (wi < 0) { colony_msg("Todas as operarias estao ocupadas."); break; }
        worker_t *w = &c->workers[wi];
        w->flags = F_LEAVE;
        w->res_target = (uint8_t)(i + 1);
        c->res[i].claimed = (uint8_t)(wi + 1);
        set_path(w, 0);
        w->flags |= F_LEAVE;
        colony_msg("Formiga %d foi buscar %s.", w->name_id, colony_res_name(c->res[i].kind));
        break;
    }
    case CMD_TAP_GROUND: {
        if (colony_raining()) { colony_msg("Chovendo: ninguem sai."); break; }
        if (colony_is_night()) { colony_msg("A noite a colonia fica selada."); break; }
        int wi = -1;
        for (int k = 0; k < c->n_workers; k++) {
            if (c->workers[k].alive && !worker_busy(&c->workers[k])) { wi = k; break; }
        }
        if (wi < 0) { colony_msg("Sem operarias livres para explorar."); break; }
        worker_t *w = &c->workers[wi];
        c->marker_x = (int16_t)clampi(cmd->val, 24, WORLD_W - 24);
        c->marker_y = (int16_t)clampi(cmd->val2, 96, 120);
        c->marker_on = 1;
        w->flags = F_LEAVE;
        w->res_target = 0;
        set_path(w, 0);
        w->flags |= F_LEAVE;
        colony_msg("Formiga %d saiu em exploracao.", w->name_id);
        break;
    }
    case CMD_SET_SPEED:
        c->speed = (uint8_t)cmd->val;
        colony_msg("Velocidade: %s", c->speed == 0 ? "pausado" :
                   c->speed == 1 ? "normal" : c->speed == 2 ? "rapida" : "turbo");
        break;
    case CMD_SAVE_NOW:
        colony_save_now();
        break;
    default:
        break;
    }
}

/* ============================ passo da simulação ========================== */
void colony_step(int ms)
{
    colony_t *c = &g_col;
    int ticks = ms / COLONY_TICK_MS;
    if (ticks < 1) ticks = 1;
    if (ticks > 8) ticks = 8;

    for (int t = 0; t < ticks; t++) {
        c->total_ticks++;

        if (c->speed) {
            c->acc += GAME_UNITS_PER_TICK * (uint32_t)c->speed;
            while (c->acc >= 100) {
                c->acc -= 100;
                c->sec_of_day++;
                if (c->sec_of_day >= 86400) c->sec_of_day = 0;
                if ((c->sec_of_day % 3600) == 0) on_hour();
            }
        }

        queen_update();
        for (int i = 0; i < c->n_workers; i++) {
            if (c->workers[i].alive) worker_update(&c->workers[i], i);
        }
        if ((c->total_ticks % 40) == 0) workers_compact();

        parts_update();
        if (c->msg_ticks) c->msg_ticks--;
        if (c->flash_ticks) c->flash_ticks--;

        /* câmera: arrasto manual segura o auto-seguir por ~20 s */
        if (s_cam_hold) { s_cam_hold--; }
        else {
            int target = -1;
            for (int i = 0; i < c->n_workers; i++) {
                worker_t *w = &c->workers[i];
                if (!w->alive) continue;
                if (w->state == W_GO_RES || w->state == W_COLLECT ||
                    w->state == W_RETURN || w->state == W_SCOUT ||
                    w->state == W_HUNT || w->state == W_STUCK ||
                    w->state == W_FLEE) { target = UNQ8(w->x) - VIEW_W / 2; break; }
            }
            if (c->marker_on) target = c->marker_x - VIEW_W / 2;
            if (target >= 0) {
                target = clampi(target, 0, WORLD_W - VIEW_W);
                int d = target - c->cam_x;
                if (d > 2)       c->cam_x = (int16_t)(c->cam_x + d / 6 + 1);
                else if (d < -2) c->cam_x = (int16_t)(c->cam_x + d / 6 - 1);
            }
        }

        if (colony_raining() && (c->total_ticks % 3) == 0) {
            part_add(P_DROP, rnd_range(0, VIEW_W), VIEW_Y + rnd_range(0, 24),
                     -6, 60, 44, COL_WATER, NULL);
        }

        if (++c->autosave_ticks >= 600) {      /* ~30 s */
            c->autosave_ticks = 0;
            colony_save_now();
        }
    }
}

/* ============================ hit tests =================================== */
int colony_chamber_at(int x, int y)
{
    for (int i = 0; i < g_col.n_chambers; i++) {
        chamber_t *ch = &g_col.chambers[i];
        if (x >= ch->x && x < ch->x + ch->w && y >= ch->y && y < ch->y + ch->h) return i;
    }
    return -1;
}

int colony_worker_at(int x, int y)
{
    int best = -1, bd = 13 * 13;
    for (int i = 0; i < g_col.n_workers; i++) {
        if (!g_col.workers[i].alive) continue;
        int dx = UNQ8(g_col.workers[i].x) - x;
        int dy = UNQ8(g_col.workers[i].y) - y;
        int d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

bool colony_queen_at(int x, int y)
{
    int dx = UNQ8(g_col.queen.x) - x;
    int dy = UNQ8(g_col.queen.y) - y;
    return (dx * dx + dy * dy) < 20 * 20;
}

/* ============================ tempo offline =============================== */
void colony_offline(uint32_t sec)
{
    if (sec < 300) return;
    if (sec > 48u * 3600u) sec = 48u * 3600u;
    uint32_t h = sec / 3600;
    if (!h) h = 1;

    colony_t *c = &g_col;
    queen_t *q = &c->queen;

    q->hunger  = (uint8_t)clampi(q->hunger - (int)(3 * h), 5, 100);
    q->energy  = (uint8_t)clampi(q->energy + (int)(6 * h), 0, 100);
    q->mood    = (uint8_t)clampi(q->mood - (int)h, 10, 100);
    q->hygiene = (uint8_t)clampi(q->hygiene - (int)h, 5, 100);
    c->hygiene = (uint8_t)clampi(c->hygiene - (int)h, 5, 100);

    int eat = (int)h * (colony_population() + 2) / 3;
    c->food = (int16_t)clampi(c->food - eat, 0, colony_food_cap());

    /* ninhada avança (~6 h por estágio) */
    uint32_t steps = h / 6;
    for (uint32_t i = 0; i < steps; i++) {
        if (c->eggs) { c->eggs--; c->larvae++; }
        else if (c->larvae) { c->larvae--; c->pupae++; }
        else if (c->pupae && c->n_workers < colony_worker_cap()) {
            c->pupae--;
            worker_t *w = &c->workers[c->n_workers];
            memset(w, 0, sizeof(*w));
            w->alive = 1;
            w->name_id = (uint8_t)(c->n_workers + 1);
            w->node = w->node_to = 3;
            w->x = Q8(g_node_x[3]); w->y = Q8(g_node_y[3]);
            w->state = W_IDLE;
            w->energy = 90; w->hunger = 25; w->health = 100;
            c->n_workers++;
        } else break;
    }

    /* jardim de fungos produziu */
    int fi = -1;
    for (int i = 0; i < c->n_chambers; i++) {
        if (c->chambers[i].kind == CH_FUNGUS) fi = i;
    }
    if (fi >= 0) add_food(c->chambers[fi].level * (int)h / 2);

    /* recursos externos renasceram */
    for (int i = 0; i < c->n_res; i++) {
        resource_t *r = &c->res[i];
        r->claimed = 0;
        if (!r->amount && r->respawn_h <= h) r->amount = r->max_amount;
    }

    /* operárias: descansadas e de volta para casa */
    for (int i = 0; i < c->n_workers; i++) {
        worker_t *w = &c->workers[i];
        if (!w->alive) continue;
        w->energy = 100;
        w->hunger = 25;
        w->carry = 0; w->carry_food = 0; w->carry_mat = 0;
        if (worker_state_is_outside(w->state)) {
            w->state = W_IDLE;
            w->node = w->node_to = 5;
            w->x = Q8(g_node_x[5] + (int)colony_rand_range(20) - 10);
            w->y = Q8(g_node_y[5] + (int)colony_rand_range(8) - 4);
        } else if (w->state != W_SLEEP) {
            w->state = W_IDLE;
            w->x = Q8(g_node_x[w->node]);
            w->y = Q8(g_node_y[w->node]);
        }
        w->timer = (uint16_t)colony_rand_range(60);
    }
    workers_compact();

    c->day += sec / 86400u;
    c->egg_progress = 0;
    colony_msg("Voce ficou fora %lu h: a colonia seguiu vivendo!", (unsigned long)h);
}

/* ===================== controles de sessão (v0.3.0) ======================== */
void colony_set_forage_cap(int n)
{
    s_forage_cap = (uint8_t)(n < 1 ? 1 : (n > 20 ? 20 : n));
}

int colony_get_forage_cap(void) { return s_forage_cap; }

int colony_spider_hp(void) { return g_col.spider_x >= 0 ? (int)s_spider_hp : -1; }

void colony_cam_pan(int dx)
{
    g_col.cam_x = (int16_t)clampi(g_col.cam_x - dx, 0, WORLD_W - VIEW_W);
    s_cam_hold = 400;          /* ~20 s sem auto-seguir */
}
