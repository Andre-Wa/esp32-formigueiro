/*
 * colony.h — Modelo de dados e API da simulação do formigueiro.
 *
 * Convenções:
 *  - Posições em ponto fixo Q8 (1 px = 256). Nunca float na simulação:
 *    determinismo + velocidade no ESP32 sem FPU de dupla precisão.
 *  - Status da rainha: inteiros 0..100.
 *  - Tempo de jogo em SEGUNDOS absolutos (0..86399 = hora do dia) + dia.
 *  - O tick da simulação é fixo: COLONY_TICK_MS = 50 ms (20 fps).
 *
 * Todo o estado cabe numa única struct `colony_t` (~1,1 KB) gravada de uma
 * vez na partição "save".
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "app_config.h"
#include "app_types.h"

#define COLONY_TICK_MS      50
#define COLONY_MAX_WORKERS  20
#define COLONY_MAX_CHAMBERS 8
#define COLONY_MAX_NODES    12
#define COLONY_MAX_RES      14
#define COLONY_MAX_PARTS    24
#define COLONY_MAX_EVENTS   12

#define WORLD_W             640     /* largura do mundo exterior (px) */
#define VIEW_W              320
#define VIEW_H              178
#define VIEW_X              0
#define VIEW_Y              24      /* abaixo da barra de status */

#define Q8(v)               ((int32_t)((v) * 256))
#define UNQ8(v)             ((int)((v) >> 8))

/* ============================ tempo de jogo =============================== */
/* A cada tick o acumulador sobe `speed` unidades; com speed=1 um dia de jogo
 * (24 h) dura ~8 minutos reais. speed=0 pausa, 2 dobra, 4 quadruplica. */
#define GAME_UNITS_PER_TICK 90      /* 1/100 de segundo de jogo por unidade */

typedef enum {
    PHASE_NIGHT = 0,    /* 21h..05h: colônia selada, tudo dorme */
    PHASE_DAWN,         /* 06h..08h: acordam, comem, cuidam da rainha */
    PHASE_DAY,          /* 09h..15h: pico de coleta */
    PHASE_AFTERNOON,    /* 16h..18h: obras e berçário */
    PHASE_EVENING,      /* 19h..20h: jantar e convivio */
} colony_phase_t;

/* ============================ cenário interno ============================= */
typedef enum {
    CH_NONE = 0,
    CH_ATRIUM,      /* átrio: entroncamento de túneis */
    CH_STORAGE,     /* despensa: comida + material */
    CH_FUNGUS,      /* jardim de fungos: produção lenta de comida */
    CH_NURSERY,     /* berçário: ovos, larvas, pupas */
    CH_ROYAL,       /* câmara real: a rainha */
    CH_TRASH,       /* lixeira: higiene da colônia */
} chamber_kind_t;

typedef struct {
    uint8_t kind;
    int16_t x, y, w, h;     /* retângulo da câmara (px, vista interior) */
    int16_t cx, cy;         /* centro (nó de navegação) */
    uint8_t level;          /* 1..3 (expansão por material) */
    uint8_t node;           /* nó do grafo correspondente */
    uint8_t water;          /* jardim de fungos: umidade 0..100 */
    uint8_t dirty;          /* sujeira 0..100 (afeta higiene/moral) */
} chamber_t;

/* ============================ recursos externos =========================== */
typedef enum {
    RES_NONE = 0,
    RES_LEAF,       /* folha: +material, pouca comida */
    RES_BERRY,      /* fruta: muita comida */
    RES_FLOWER,     /* flor: comida (néctar) + moral */
    RES_SEED,       /* sementes: comida durável */
    RES_TWIG,       /* graveto: muito material */
    RES_MUSHROOM,   /* cogumelo: comida + risco pequeno */
    RES_PREY,       /* presa (besouro morto): caça, muita comida */
    RES_STONE,      /* pedrinha: material de construção */
} res_kind_t;

typedef struct {
    uint8_t  kind;
    int16_t  x, y;          /* posição no mundo exterior */
    uint8_t  amount;        /* 0 = esgotado */
    uint8_t  max_amount;
    uint16_t respawn_h;     /* horas de jogo para renascer */
    uint8_t  timer_h;       /* contador */
    uint8_t  claimed;       /* índice+1 da operária a caminho */
} resource_t;

/* ============================ formigas ==================================== */
typedef enum {
    W_IDLE = 0,     /* andando à toa dentro da câmara */
    W_GOTO,         /* seguindo o caminho de nós até `node_to` */
    W_EXIT,         /* subindo pelo túnel de entrada até a superfície */
    W_GO_RES,       /* no exterior: indo até o recurso alvo */
    W_COLLECT,      /* coletando (parada, temporizador) */
    W_HUNT,         /* caçando presa */
    W_RETURN,       /* no exterior: voltando para o monte */
    W_ENTER,        /* descendo o túnel de entrada */
    W_DEPOSIT,      /* entregando a carga na despensa */
    W_TEND,         /* cuidando de ovos/larvas */
    W_TEND_QUEEN,   /* cuidando/alimentando a rainha */
    W_BUILD,        /* escavando/expandido uma câmara */
    W_CLEAN,        /* carregando lixo para fora */
    W_EAT,          /* comendo na despensa */
    W_SLEEP,        /* dormindo */
    W_SCOUT,        /* batedora: indo até um ponto tocado pelo usuário */
    W_FLEE,         /* fugindo de um predador */
    W_STUCK,        /* presa na chuva/tormenta: espera passar */
} worker_state_t;

typedef struct {
    uint8_t  alive;
    uint8_t  name_id;       /* sufixo do nome (Formiga N) */
    int32_t  x, y;          /* Q8 */
    int8_t   face;          /* -1 esquerda, +1 direita */
    uint8_t  state;         /* worker_state_t */
    uint8_t  node;          /* nó atual */
    uint8_t  node_to;       /* nó destino */
    int8_t   path[COLONY_MAX_NODES];
    uint8_t  path_len, path_i;
    uint8_t  carry;         /* res_kind_t carregado */
    uint8_t  carry_food, carry_mat;
    uint8_t  energy;        /* 0..100 */
    uint8_t  hunger;        /* 0..100 (100 = faminta) */
    uint8_t  health;        /* 0..100 */
    uint8_t  res_target;    /* índice em res[] + 1 */
    int16_t  tx, ty;        /* alvo em px (exterior / posição livre) */
    uint16_t timer;         /* ticks restantes da ação corrente */
    uint8_t  anim;          /* fase da animação */
    uint8_t  flags;         /* 0x10 sair p/ exterior | 0x20 andar livre |
                             * 0x40 indo comer | 0x80 indo dormir */
    uint8_t  hurt;          /* 1 = ferida (desenha curativo) */
} worker_t;

typedef enum {
    Q_IDLE = 0,
    Q_WALK,
    Q_SLEEP,
    Q_EAT,
    Q_PLAY,
    Q_GROOM,
    Q_PET,        /* recebendo carinho */
    Q_TO_NURSERY, /* indo pôr ovos */
    Q_LAY,        /* pondo ovos */
    Q_HUNTING,    /* acompanha a expedição de caça (fica na entrada) */
    Q_SAD,
} queen_state_t;

typedef struct {
    int32_t  x, y;          /* Q8 */
    int8_t   face;
    uint8_t  state;         /* queen_state_t */
    uint16_t timer;         /* ticks da ação corrente */
    uint8_t  node, node_to;
    int8_t   path[COLONY_MAX_NODES];
    uint8_t  path_len, path_i;
    uint16_t wander_t;      /* ticks até decidir o próximo passo */
    int16_t  tx, ty;        /* alvo do passeio livre (px da vista interior) */
    uint8_t  hunger;        /* 0..100 (100 = satisfeita) */
    uint8_t  energy;        /* 0..100 */
    uint8_t  mood;          /* 0..100 */
    uint8_t  hygiene;       /* 0..100 */
    uint8_t  health;        /* 0..100 */
    uint16_t age_h;         /* horas de vida */
    uint16_t eggs_laid;     /* total de ovos postos */
    uint8_t  bond;          /* 0..100: vínculo com o usuário (carinho/jogo) */
    uint16_t pet_cd;        /* ticks até poder receber carinho de novo */
    uint8_t  last_action;   /* última ação do usuário (p/ balão de fala) */
    uint16_t say_timer;     /* ticks com balão de fala ativo */
    char     say[28];       /* texto do balão */
} queen_t;

/* ============================ partículas ================================== */
typedef enum {
    P_NONE = 0,
    P_HEART,
    P_ZZZ,
    P_SPARK,
    P_DIRT,
    P_DROP,
    P_TEXT,       /* texto flutuante (+n / -n) */
    P_NOTE,
} part_kind_t;

typedef struct {
    uint8_t kind;
    int16_t x, y;
    int8_t  vx, vy;     /* 1/16 px por tick */
    uint8_t life;
    uint8_t max_life;
    uint16_t col;
    char    txt[8];
} particle_t;

/* ============================ colônia ===================================== */
typedef struct {
    uint32_t rng;

    /* tempo */
    uint32_t day;             /* 1.. */
    uint32_t sec_of_day;      /* 0..86399 */
    uint32_t acc;             /* acumulador de 1/100 s de jogo */
    int64_t  epoch_save;      /* epoch UTC (s) no momento do save */
    uint32_t total_ticks;

    /* rainha e população */
    queen_t  queen;
    worker_t workers[COLONY_MAX_WORKERS];
    uint8_t  n_workers;

    /* câmaras */
    chamber_t chambers[COLONY_MAX_CHAMBERS];
    uint8_t   n_chambers;

    /* recursos externos */
    resource_t res[COLONY_MAX_RES];
    uint8_t    n_res;
    int16_t    spider_x, spider_y;
    int8_t     spider_dir;

    /* economia */
    int16_t food;             /* 0..food_cap */
    int16_t material;         /* 0..300 */
    int16_t food_cap;
    uint8_t eggs, larvae, pupae;
    uint16_t egg_progress;    /* 0..1000 por ovo mais adiantado */
    uint8_t hygiene;          /* 0..100 (da colônia inteira) */
    uint16_t points;          /* pontuação do habitat */
    uint16_t tasks_done;

    /* clima / rede */
    uint8_t  weather;         /* código WMO simplificado (ver weather.h) */
    int16_t  temp_x10;        /* °C x10 */
    uint8_t  humidity;
    uint8_t  precip_prob;
    uint8_t  net_ok;          /* WiFi conectado */
    uint8_t  time_sync;       /* hora vinda do NTP */
    uint16_t weather_age_min;

    /* interface */
    uint8_t  speed;           /* 0/1/2/4 */
    int16_t  cam_x;           /* câmera do mundo exterior */
    char     msg[48];         /* última mensagem (ticker) */
    uint16_t msg_ticks;
    char     events[COLONY_MAX_EVENTS][30];
    uint8_t  event_head;
    uint8_t  event_count;
    int64_t  last_save_ms;
    uint16_t autosave_ticks;

    /* partículas */
    particle_t parts[COLONY_MAX_PARTS];
    uint8_t    n_parts;

    /* seleção / interação */
    uint8_t  sel_worker;      /* índice+1 da operária selecionada */
    uint8_t  sel_chamber;     /* índice+1 da câmara selecionada */
    int16_t  marker_x, marker_y;   /* marcador de destino no exterior */
    uint8_t  marker_on;
    uint8_t  alarm;           /* 0 = ok, 1 = fome, 2 = predador, 3 = tormenta */
    uint8_t  flash_ticks;     /* piscada de feedback (LED/tela) */
} colony_t;

/* ============================ API ========================================= */
extern colony_t g_col;

/* Cria a colônia inicial (novo jogo). */
void colony_new(void);

/* Restaura do buffer de save; se falhar, cria do zero. */
bool colony_load(const void *buf, size_t len);

/* Serializa no buffer (retorna o tamanho, 0 se não couber). */
size_t colony_serialize(uint8_t *buf, size_t cap);

/* Avança a simulação. `ms` = tempo real decorrido desde o último tick. */
void colony_step(int ms);

/* Aplica um comando vindo do touch. */
void colony_command(const app_cmd_t *cmd);

/* Grava o save na flash (com debounce interno em colony_step). */
void colony_save_now(void);

/* Aplica (de forma resumida) o tempo em que a placa ficou desligada.
 * Chamado quando o NTP acerta o relógio pela primeira vez. */
void colony_offline(uint32_t seconds);

/* controles de sessão (v0.3.0) */
void colony_set_forage_cap(int n);   /* teto de operárias no exterior (1..20) */
int  colony_get_forage_cap(void);
int  colony_spider_hp(void);         /* -1 = derrotada/ausente */
void colony_cam_pan(int dx);         /* panorâmica manual no exterior */

/* ---------------- utilidades ---------------- */
colony_phase_t colony_phase(void);
uint8_t  colony_hour(void);
uint8_t  colony_minute(void);
uint8_t  colony_population(void);      /* rainha + operárias */
const char *colony_phase_name(void);
const char *colony_weather_name(void);
bool colony_is_night(void);
bool colony_raining(void);

void colony_msg(const char *fmt, ...);           /* ticker + log de eventos */
void colony_event(const char *fmt, ...);         /* só o log */
uint8_t colony_food_cap(void);

/* Grafo de túneis (nós) — exposto para o renderizador */
extern const int16_t g_node_x[COLONY_MAX_NODES];
extern const int16_t g_node_y[COLONY_MAX_NODES];
extern const uint8_t g_node_count;

/* caminho de `from` até `to` em `out` (retorna o número de nós) */
int colony_find_path(uint8_t from, uint8_t to, int8_t *out, int cap);

/* índice da câmara cujo retângulo contém (x,y), ou -1 */
int colony_chamber_at(int x, int y);
/* índice da operária em (x,y), ou -1 */
int colony_worker_at(int x, int y);
/* rainha tocada? */
bool colony_queen_at(int x, int y);

/* sorteio determinístico */
uint32_t colony_rand(void);
uint32_t colony_rand_range(uint32_t n);

/* efeitos visuais (coordenadas de TELA: some VIEW_Y ao usar coords locais) */
void colony_fx_heart(int x, int y);
void colony_fx_spark(int x, int y, uint16_t col);
void colony_fx_dirt(int x, int y);
void colony_fx_zzz(int x, int y);
void colony_fx_text(int x, int y, const char *txt, uint16_t col);

/* informações para a UI */
const char *colony_chamber_name(int idx);
const char *colony_res_name(uint8_t kind);
void colony_res_yield(uint8_t kind, uint8_t *food, uint8_t *mat, uint8_t *ticks);
uint8_t colony_build_cost(int chamber_idx);   /* 0 = nível máximo */
uint8_t colony_brood_cap(void);
uint8_t colony_worker_cap(void);
const char *colony_worker_state_name(uint8_t st);
