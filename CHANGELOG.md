# Changelog — Formigueiro (CYD)

## 0.3.1 — 2026-11-03

**Build (correção de loop de reconfigure do CMake/ninja)**
- `sdkconfig.defaults` limpo: símbolo atualizado
  (`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240`, o antigo
  `CONFIG_ESP32_DEFAULT_CPU_FREQ_240` estava depreciado), removidos símbolos
  inexistentes no IDF 5.5 (`HTTPD_MAX_REQ_HDRS`, `LWIP_SNTP_SUPPORT`) e
  comentários decorados (`# ==== ... ====`) que o kconfgen reescrevia a cada
  configure — cada reescrita renovava o mtime de `sdkconfig` e podia
  realimentar o ciclo "Re-running CMake…".
- Receita contra `ninja: error: manifest 'build.ninja' still dirty after 100
  tries`: build limpo (`rm -rf build sdkconfig`), relógio do PC conferido
  (`date`) e, se o log mostrar `fatal: gitfile não aponta para um repositório
  válido` no IDF, remover o arquivo `.git` quebrado da instalação do ESP-IDF
  (ou reinstalar) — detalhes em `INSTALACAO.md` §7.

## 0.3.0 — 2026-11-03

**WiFi/portal**
- Crash corrigido: 2º toque em Configurações → WIFI recriava o netif AP
  (`assert ... duplicate key` em `esp_netif_create_default_wifi_ap`). Agora o
  netif é único e `provision_off()` derruba o portal corretamente.
- WIFI virou **toggle** (abre/fecha o portal) e, **sem credenciais, o portal
  abre sozinho no boot**.
- **Captive portal DNS** (porta 53): o celular abre 192.168.4.1 sozinho.

**Brilho**
- Botões − / + reais (zonas de toque = glifos desenhados), passo 10, limites
  10–100, sem o ciclo `% 101` que "reiniciava" o valor.
- **AUTO**: brilho pelo sensor de luz (LDR GPIO34/ADC1_CH6), novo módulo
  `hw/ldr.c`, tecla AUTO na linha BRILHO.

**Jogo**
- Formigas não atravessam mais paredes: `CORR` (render) agora tem as mesmas
  12 arestas do `ADJ` (simulação).
- Flip dos sprites corrigido (a arte base olha p/ esquerda; antes andavam
  "de costas" para o lado certo).
- Exterior: **arrastar move a câmera** (1:1, com pausa de ~20 s no auto-seguir).
- **Aranha tem vida**: operárias próximas revidam; derrota = +25 comida e ela
  some por ~2 dias de jogo. Toque nela mostra a vida.
- **Obras confiáveis**: AMPLIAR cobra o custo (30/60) na partida, operárias
  escavam na própria câmara, nível sobe ao concluir (sem progresso escondido
  no `water` do átrio).
- Energia cai ~2× mais devagar; **EQUIPE FORA** limita operárias na rua
  (2/3/4/6/todas) — controle de grupo.
- **Jardim de fungos com função clara**: rende `2·nível +umidade +chuva` a
  cada 2 h; operárias podem umedecê-lo (nó 2); tocar mostra produção/umidade.
- Tocar nas câmaras mostra info útil (estoques, ninhada, humor da rainha,
  higiene) em vez de só o nome.

**Docs**: `docs/ARQUITETURA.md`, `docs/JOGO.md`, `docs/REDE.md` (inclui o plano
da PWA + BLE para a v0.4.0).

Obs.: o tamanho de `app_config_t` mudou (`bright_auto`) → configurações voltam
aos padrões uma vez após atualizar (o save da colônia NÃO é afetado).

## 0.2.0 — 2026-11-03

**Correção crítica: touch morto na tela do jogo + task watchdog em loop.**

- `main/main.c`: o ritmo do laço do jogo usava `vTaskDelayUntil(50 ms)`, que
  não dorme quando o quadro demora mais que o tick (o redraw completo da cena
  passa de 50 ms). A task nunca bloqueava, matando de fome a `IDLE1`
  (`task_wdt` a cada 10 s) e a `touch_task` (mesmo core). Substituído por
  pacing que mede o tempo do quadro e **garante ≥ 4 ms de bloqueio** por
  iteração.
- `main/app_config.h`: `PRIO_TOUCH` 5 → **7** (acima de `PRIO_GAME` 6): a
  amostragem do XPT2046 (20 ms) agora preempta o render mesmo em quadros
  longos — toque responde imediato.
- `main/app_config.h`: `APP_VERSION` → `0.2.0` (confira no monitor serial o
  banner `Formigueiro v0.2.0`).

**Como aplicar**: extraia este zip por cima da pasta `formigueiro/` do projeto
(sobrescrevendo `main/main.c` e `main/app_config.h`), depois:

```bash
idf.py build
idf.py -p SUAPORTA flash monitor
```

Não é preciso `erase-flash` — a calibração de touch salva na NVS continua
valendo. Se o toque responder mas mirar no lugar errado, recalibre em
**☰ → Configurações → TOUCH**.

## 0.1.0 — 2026-11-03

Versão inicial: jogo completo (rainha + colônia, interior/exterior, rotinas,
economia, clima real via Open-Meteo, relógio SNTP, portal WiFi de
configuração, LED RGB de status, saves em partição dedicada), compilando sem
erros/warnings no ESP-IDF v5.5.1.
