# Arquitetura do software — Formigueiro v0.3.0

Como o firmware funciona por dentro. Público: quem vai ler/modificar o código.
Complementos: `JOGO.md` (gameplay), `REDE.md` (WiFi/portal/PWA-BLE).

## 1. Visão geral

- **Alvo**: ESP32 (dual-core, sem PSRAM) na placa ESP32-2432S028R (CYD):
  ILI9341 320×240 via SPI2+DMA, touch XPT2046 por SPI bit-bang, LED RGB,
  LDR no GPIO34. ESP-IDF v5.5.1, C17, sem C++/sem RTOS alternativo.
- **Estilo gráfico**: pixel art 16-bit (RGB565 com bytes trocados para o
  painel). Toda a arte nasce como código em `tools/gen_sprites.py`, que gera
  `main/game/sprites.c` (paletas + pixels indexados + blitter com recorte).
- **Fonte**: `font8x16.c` (ASCII) + `fontext.c` (0x80–0xFF, acentos), geradas
  por `tools/gen_font.py` / `gen_font_ext.py`.

## 2. Tarefas FreeRTOS

| Task | Core | Prio | O que faz |
|---|---|---|---|
| `main_task` | 0 | padrão | `app_main()`: init de tudo e cria as tarefas |
| `game` | 1 | 6 | laço do jogo: comandos → simulação → render → UI |
| `touch` | 1 | **7** | amostra o XPT2046 a cada 20 ms e gera gestos |
| `net` | 0 | 4 | WiFi/portal/SNTP/clima (Open-Meteo) |
| `cpdns` | — | 3 | DNS do captive portal (só enquanto o portal está aberto) |
| tasks do IDF | 0 | altas | wifi, tcpip, timers |

Regra de ouro (lição da v0.2.0): **toda task de laço precisa bloquear**. O
laço `game` mede o tempo do quadro e dorme `COLONY_TICK_MS − tempo`, no mínimo
4 ms (`main.c`). Sem isso a IDLE1 morre de fome e o `task_wdt` dispara — e o
touch (mesmo core) nunca roda.

## 3. Laço do jogo (`main.c` → `game_task`)

A cada iteração (~50 ms nominais):

1. drena a fila `g_cmd_queue` (comandos da UI: `app_cmd_t`) — sistema vs. colônia;
2. drena a fila `g_evt_queue` (eventos de rede/tempo: `app_evt_t`);
3. fluxo de boot de telas (orient → calib → game, conforme a NVS);
4. `colony_step(elapsed_ms)` — simulação (tick interno de 50 ms de jogo);
5. `game_screen_refresh()` — marca regiões sujas (tiras) conforme mudou algo;
6. `ui_pump()` — troca de tela, gestos de touch, e render das tiras sujas;
7. a cada 200 ms: `led_update()` + brilho automático (LDR → `lcd_set_backlight`).

Tempo de jogo: `GAME_UNITS_PER_TICK = 90` (0,9 s de jogo por tick de 50 ms
reais) ⇒ **1 dia = 96 000 ticks ≈ 80 min reais** em x1 (x2/x4/pausa alteram o
multiplicador aplicado em `colony_step`).

## 4. Render por tiras (`ui/ui_engine.c`)

- Framebuffer **não** é 320×240 inteiro: são 15 tiras de 16 px
  (`LCD_STRIP_BYTES = 10240`), 2 buffers DMA alternados.
- `ui_dirty_rect()` marca tiras; `ui_pump()` → `render_strips(mask)`: para cada
  tira suja, chama `screen->draw(canvas)` recortado e envia via
  `lcd_flush_async()` (SPI polling, CS/DC por GPIO, MADCTL por orientação).
- Telas (`ui_screen_t`): `splash`, `game`, `settings`, `calib`, `orient` —
  cada uma com `draw` e `on_touch`; troca por fila `s_ctrl_q` (`ui_show()`).

## 5. Cadeia do touch

`touch_xpt2046.c` (SPI bit-bang, Z≥600, média de 3 amostras) →
`touch_task` (gestos: PRESS/TAP/DRAG/LONG/RELEASE, debounce 60 ms, long-press
900 ms) → fila `s_touch_q` → `ui_pump()` → `screen->on_touch(evt)`.
Calibração afim 2×3 salva na NVS (`calib`), aplicada em `xpt2046_read_full()`.

## 6. Simulação (`game/colony.c`)

- Estado global `g_col` (`colony_t`): rainha, operárias, câmaras, recursos,
  clima, fila de partículas, contadores. **Save = blob binário** em partição
  própria (ping-pong de 2 setores de 4 KB, cabeçalho com CRC/seq).
- Interior: grafo de 8 nós (`ADJ` em colony.c) = túneis; o **mesmo** grafo é
  desenhado em `render.c` (`CORR`) — se divergirem, formigas “andam na parede”.
  Caminhos por Floyd-Warshall greedy (`colony_find_path`).
- Exterior: mundo de 640 px, câmera 0..320 (auto-segue operária externa, com
  prioridade de ~20 s para arrasto manual — `colony_cam_pan()`).
- Estados de operária: `W_IDLE/GOTO/SLEEP/EAT/EXIT/GO_RES/COLLECT/HUNT/RETURN/
  SCOUT/STUCK/FLEE/TEND/TEND_QUEEN/CLEAN/BUILD`. Decisão por fase do dia +
  chuva + **teto de equipe externa** (`colony_set_forage_cap`, padrão 3).
- Aranha: patrulha x∈[470,620]; ataca operárias próximas (com cooldown `hurt`);
  operárias próximas **revidam** (vida em `colony_spider_hp()`, sessão, não vai
  no save); ao morrer dá +25 comida e some (`spider_x = -1`), voltando ~2 dias
  de jogo depois.
- Economia por tick de 2 h de jogo: comida consumida `(pop+2)/3`; jardim de
  fungos produz `2·nível + (umidade>30) + chuva`; sujeira/higiene; ninhada
  ovo→larva→pupa com taxa do berçário + cuidadoras.
- Obras: `do_build()` (AMPLIAR) cobra o custo (30/60 material) na partida,
  manda ≤2 operárias com flag `F_BUILD` ao nó da câmara; ao chegar, `W_BUILD`;
  a primeira a concluir sobe o nível (`s_build_ci` evita obra dupla).

## 7. UI do jogo (`ui/screens/game_screen.c`)

- Barra de status (24 px) | cena y24..201 | ticker | barra inferior y202..239.
- Gestos: tap (rainha/operária/câmara/recurso/aranha/botões), long-press ou ☰
  (menu 3×4), arrasto: interior troca de vista; **exterior faz panorâmica**.
- Comandos vão pela fila `g_cmd_queue` até o `game_task` (nunca se age direto
  no estado a partir do touch — isso mantém a simulação single-writer).

## 8. Config e persistência

- `nvs_config`: blob `app_config_t` (brilho, bright_auto, orientação, calib,
  WiFi, cidade, fuso, led…) + chaves-espelho legíveis (`bright`, `xform`,
  `orientok`, `calib`). Mudar o tamanho do struct **invalida o blob** (volta
  aos padrões, mantendo as chaves soltas) — por isso versões novas resetam
  configurações uma vez.
- `game_save`: partição `save` (0x40, 64 KB); dois setores ping-pong; gravação
  periódica + em transições; `colony_offline(sec)` aplica até 48 h de ausência.

## 9. Hardware extra

- `hw/led.c`: LED RGB por estado (verde/azul/ciano/âmbar/vermelho/roxo/amarelo).
- `hw/ldr.c`: ADC1_CH6 oneshot, 12-bit, atenuação 12 dB; brilho alvo
  `12 + luz%·0,88`, aplicado a cada 200 ms quando `bright_auto`.
- `hw/sfx.h`: stubs de buzzer (GPIO26) e vibração (GPIO22) — porta futura.

## 10. Rede (`net/`)

Resumo aqui; detalhes e o plano PWA/BLE em `REDE.md`.
- `wifi_prov.c`: STA + AP de provisionamento + httpd (portal) + DNS captive +
  SNTP + retry periódico; `weather.c`: Open-Meteo (HTTPS c/ bundle de CAs,
  fallback HTTP) + simulador Markov offline.

## 11. Build

- `sdkconfig.defaults`: 240 MHz, partições custom, WiFi+SoftAP, mbedTLS com
  buffers dinâmicos, sem BT/PSRAM.
- `partitions.csv`: nvs 24 KB, phy 4 KB, factory 2,5 MB, save 64 KB.
- Assets regeneráveis: `tools/gen_sprites.py`, `gen_font_ext.py`, `gen_font.py`.
