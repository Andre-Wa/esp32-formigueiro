# Changelog — Formigueiro (CYD)

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
