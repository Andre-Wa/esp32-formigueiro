# 🐜 Formigueiro — Pet Virtual para o CYD

Jogo de **bichinho de virtual** para a placa **ESP32-2432S028R** ("Cheap Yellow
Display" / CYD): você cuida de uma **rainha** e de sua colônia de formigas, com
duas telas — o **interior do formigueiro** (câmaras: despensa, fungo, berçário,
câmara real, lixeira) e o **exterior** (mundo coletável com folhas, frutas,
flores, sementes, gravetos, cogumelos, pedras e besouros).

Feito 100% em **C puro com ESP-IDF v5.5.1** (sem Arduino/PlatformIO), com
gráficos **pixel art 16-bit** gerados por script, relógio em tempo real (SNTP +
fuso), **clima real via WiFi** (Open-Meteo) e **LED RGB** de status.

---

## Recursos

- **Rainha interativa**: toque direto nela (carinho, fala em balões) **e**
  botões de ação fixos na barra inferior.
- **Operárias com rotina própria**: coletar, caçar, escavar/construir, cuidar do
  berçário, faxina, comer, dormir — cada uma com estado visível ao tocar.
- **Duas cenas**: interior (câmaras clicáveis, níveis, ampliação) e exterior
  (mundo de 640 px com rolagem por swipe, recursos que renascem, aranha à espreita).
- **Ciclo dia/noite** com fases (noite, alvorada, dia, tarde, noite) que mudam o
  céu, a temperatura e o comportamento da colônia. 1 dia de jogo ≈ 80 min reais
  (velocidade x1; x2/x4/pausa nas Configurações).
- **Economia**: comida, material, fungo (produção contínua), ninhada
  (ovo → larva → pupa → operária), higiene/sujeira, moral e energia da rainha.
- **Relógio real**: SNTP com fuso configurável (padrão Brasília `<-03>3`).
- **Clima real**: portal WiFi de configuração → geolocalização por nome de
  cidade → Open-Meteo (temperatura, umidade, chuva). Sem WiFi, um simulador
  Markov mantém o clima coerente.
- **Progresso offline**: ao religar, a colônia avança o tempo ausente (até 48 h)
  com regras simplificadas.
- **LED RGB** de status (ver tabela abaixo).
- **Save automático** em flash (partição dedicada com ping-pong de 2 setores) +
  NVS para configurações.
- **Áudio e vibração**: stubs prontos (`main/hw/sfx.h`) — buzzer no GPIO26 e
  motor no GPIO22, para ligar depois sem tocar no resto do código.

## Hardware e pinos (ESP32-2432S028R)

| Função | Pinos |
|---|---|
| LCD ILI9341 320×240 (SPI2) | MOSI 13, MISO 12, SCLK 14, CS 15, DC 2, backlight 21 (LEDC) |
| Touch XPT2046 (SPI bit-bang) | CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36 |
| LED RGB (ativo em nível baixo) | R 4, G 16, B 17 |
| Buzzer (futuro) | GPIO 26 |
| Vibração (futuro) | GPIO 22 |
| LDR (não usado) | GPIO 34 |

Flash: 4 MB (DIO, 40 MHz). Sem PSRAM — o framebuffer de 150 KB vive no heap.

## Estrutura do projeto

```
formigueiro/
├── CMakeLists.txt          # projeto ESP-IDF
├── sdkconfig.defaults      # 240 MHz, partições custom, WiFi+SoftAP, mbedTLS dinâmico
├── partitions.csv          # nvs + factory 2,5 MB + "save" 64 KB
├── main/
│   ├── main.c              # entrada: init de tudo + task do jogo (core 1)
│   ├── app_config.h        # pinos e constantes da placa
│   ├── app_types.h         # eventos/comandos da UI (fila FreeRTOS)
│   ├── display/            # driver LCD SPI + DMA, touch XPT2046, primitivas, fontes
│   ├── game/               # colony.[ch] (simulação), render.c, sprites, gfx_ext
│   ├── ui/                 # engine de telas + telas (jogo, config, calibração, orientação)
│   ├── net/                # wifi_prov (STA+AP portal+SNTP), weather (Open-Meteo/simulado)
│   ├── hw/                 # led RGB, sfx.h (stub buzzer/vibração)
│   └── storage/            # nvs_config (configurações), game_save (partição "save")
├── tools/
│   ├── gen_sprites.py      # gera main/game/sprites.c + docs/sprites.png (arte pixel)
│   └── gen_font_ext.py     # gera main/display/fontext.c (Latin-1 0x80–0xFF)
└── docs/                   # prévias PNG dos sprites e cenas
```

## Pré-requisitos

- **VS Code** com a extensão **Espressif IDF** (ou instalação manual do
  **ESP-IDF v5.5.1**).
- Driver USB-serial da placa (a maioria dos CYD usa **CH340**; algumas revisões
  usam CP210x — instale o driver correspondente se a porta não aparecer).
- Python 3 (já vem com o ESP-IDF) para regenerar assets, se quiser.

## Compilar e gravar

```bash
cd formigueiro
idf.py set-target esp32     # só na primeira vez
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor   # Windows: COMx
```

Saída do monitor: `Ctrl+]`. A primeira compilação leva ~2 min; o binário tem
~1,1 MB (58% livres na partição de 2,5 MB).

Uso estimado após o boot: IRAM 79%, DRAM estático 28% — sobra heap de folga
para o framebuffer (150 KB) e para o TLS do clima (buffers dinâmicos).

## Primeiro boot

1. **Calibração do touch** — toque nos 4 cantos quando pedido (dá para "pular";
   recalibre depois em Configurações → TOUCH).
2. **Orientação da tela** — escolha como a placa fica apoiada (recalibrável em
   Configurações → TELA).
3. **Portal WiFi** (opcional, mas recomendado para relógio/clima): o CYD cria a rede
   **`Formigueiro-XXXX`** (senha **`formiga123`**). Conecte o celular/PC, abra
   **`http://192.168.4.1`**, informe SSID/senha da sua rede e o **nome da
   cidade**. O dispositivo salva na NVS, conecta, sincroniza a hora (SNTP) e
   passa a buscar o clima (Open-Meteo, sem chave de API) a cada
   `WEATHER_REFRESH_MIN` minutos. Sem WiFi o jogo funciona igual, com relógio
   interno e clima simulado.

## Como jogar

**Layout da tela de jogo** (320×240):

```
┌──────────────────────────────────────┐
│ barra de status: hora, dia, clima,   │  ← comida/material/pop, alarmes
│ cena (interior/exterior)  y24..201   │  ← toque em tudo: rainha, operárias,
│ ticker de mensagens                  │    câmaras, recursos, aranha
├──────────────────────────────────────┤
│ 🏠 🌳 │ DAR BRIN CAÇA DORM OBRA │ ☰ OVOS │  ← barra inferior
└──────────────────────────────────────┘
```

- **Abas** 🏠/🌳 (ou **swipe** lateral na cena): alterna interior ↔ exterior.
- **Botões rápidos**: DAR (comida), BRIN (brincar), CAÇA (expedição), DORM
  (descansar), OBRA (construção/coleta).
- **☰ (ou pressão longa na cena)**: menu 3×4 — CARINHO, ALIMENTAR, BRINCAR,
  BANHO, DORMIR, CAÇAR, AMPLIAR, CUIDAR, COLETAR, POR OVOS, FECHAR.
- **OVOS**: pede postura (exige rainha alimentada ≥55 e descansada ≥25, e
  berçário com vaga — amplie com AMPLIAR/material).
- **Toques diretos**: na **rainha** = carinho (ela responde em balões); numa
  **operária** = mostra o que ela está fazendo; numa **câmara** = seleciona e
  mostra nível (tocar na **Lixeira** despacha faxina); num **recurso** no
  exterior = manda operária coletar; no **chão** = manda batedora explorar.

**Configurações** (☰ → tela de config): VELOCIDADE (x1/x2/x4/pausa), BRILHO,
WIFI (religa o portal), CIDADE (teclado na tela), CLIMA REAL (liga/desliga),
HORA (ajuste manual), TOUCH (calibrar), TELA (orientação), SAVE (salvar agora),
COLONIA (toque duplo = recomeçar colônia) e diário da colônia.

### LED RGB

| Estado | Cor |
|---|---|
| Dia, tudo bem | verde "respirando" |
| Noite no jogo | azul respirando |
| Conectando WiFi | azul piscando |
| Portal de configuração | ciano |
| Alarme 1 (fome) | âmbar |
| Alarme 2 (predador) | vermelho |
| Alarme 3 (tormenta) | roxo |
| Evento (ovos, carinho, faxina…) | piscada amarela |
| LED desligado (config) | — |

## Tabela de partições (`partitions.csv`)

| Nome | Tipo | Offset | Tamanho | Uso |
|---|---|---|---|---|
| nvs | data/nvs | 0x9000 | 24 KB | configurações (WiFi, cidade, calibração…) |
| phy_init | data/phy | 0xF000 | 4 KB | calibração RF |
| factory | app | 0x10000 | 2,5 MB | firmware |
| save | data/0x40 | 0x290000 | 64 KB | saves do jogo (2×4 KB ping-pong + histórico) |

## Regenerando os assets

Toda a arte é código — nada de binários de imagem no firmware:

```bash
python3 tools/gen_sprites.py    # → main/game/sprites.c (60 sprites, ~12 KB) + docs/*.png
python3 tools/gen_font_ext.py   # → main/display/fontext.c (acentos Latin-1)
python3 tools/gen_font.py       # → main/display/font8x16.c (requer Pillow + fonte TTF)
```

Edite os sprites em `tools/gen_sprites.py` (funções `make_*`) e rode o script; o
`.c` e as prévias em `docs/` são recriados. O enum em `main/game/sprites.h` é a
fonte da ordem — o gerador confere se nada está faltando.

## Créditos

- Padrões de display/touch/UI para esta placa:
  [Andre-Wa/cyd-player](https://github.com/Andre-Wa/cyd-player) (referência do
  driver LCD SPI+DMA, touch XPT2046 bit-bang e engine de telas).
- Clima: [Open-Meteo](https://open-meteo.com/) (gratuito, sem chave para uso
  não comercial).
