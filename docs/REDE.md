# Rede, WiFi e o plano PWA/Bluetooth — Formigueiro v0.3.0

## 1. Como o WiFi funciona HOJE (e por que parece “só um AP”)

O CYD não tem teclado nem tela grande o bastante para digitar senha de WiFi —
por isso o provisionamento é feito por um **portal web servido pela própria
placa**:

```
boot ── tem SSID salvo? ── sim ──> conecta (STA) ──> SNTP + Open-Meteo
                    │
                    não
                    ▼
        sobe AP "Formigueiro-XXXX" (senha formiga123)
        + httpd em http://192.168.4.1  +  DNS captive (porta 53)
                    ▼
   celular conecta no AP → o SO abre o portal sozinho (captive portal)
   ou abra http://192.168.4.1 manualmente
                    ▼
   informe SSID + senha da sua rede + cidade → POST /cfg
                    ▼
   AP é derrubado, STA conecta, hora sincroniza, clima passa a atualizar
   (a cada WEATHER_REFRESH_MIN = 20 min)
```

Pontos que costumam confundir:

- **O AP não “dá internet”** à placa: ele só existe para você entregar as
  credenciais. Depois de salvar, o AP some e a placa vira cliente (STA).
- Desde a v0.3.0, **sem credenciais o portal abre sozinho no boot** (antes era
  preciso ir em Configurações → WIFI).
- Desde a v0.3.0 há um **servidor DNS de captive portal**: qualquer nome
  resolvido pela placa responde `192.168.4.1`, então Android/iOS/Windows
  mostram o aviso “rede sem internet — tocar para entrar” e abrem o portal.
- **Configurações → WIFI virou um interruptor**: 1º toque abre o portal, 2º
  toque fecha (e volta a tentar a rede salva). Na v0.2.0 o 2º toque **crashava**
  (`assert esp_netif_create_default_wifi_ap ... duplicate key`): cada abertura
  criava um netif AP novo sem destruir o anterior. Corrigido: o netif é criado
  uma vez e destruído ao fechar.
- Enquanto o portal está aberto, a linha WIFI mostra `portal: Formigueiro-XX`
  e o LED fica ciano. O ticker também fala o endereço.
- Sem WiFi nada trava: relógio interno + clima simulado (Markov) + tudo offline.

Endpoints do portal: `GET /` (HTML), `GET /status` (JSON de estado),
`POST /cfg` (SSID/senha/cidade, urlencoded).

## 2. Limitações atuais (motivo da PWA)

- Digitar cidade/senha no portal é ok, mas **não há acompanhamento remoto**:
  para ver a colônia é preciso estar na frente da tela.
- O portal só existe no modo AP; conectado à sua rede, a placa não expõe nada
  (sem mDNS/sem API).

## 3. Plano v0.4.0 — Web app (PWA) + BLE/Web Bluetooth

Objetivo: abrir um app web no celular/PC que mostra e controla o formigueiro
**sem depender do WiFi da placa** (BLE) e, quando houver WiFi, também pela rede.

### 3.1 Transporte BLE (ESP32, NimBLE)

- Perfil GATT único, serviço `4f524d49-4755-4952-4f2d-XXXXXXXXXXXX`:
  - `...01` **RX (write)**: JSON de comandos do app → placa
    (`{"cmd":"feed"}`, `{"cmd":"set","speed":2}`, `{"cmd":"cfg","bright":70}`…);
  - `...02` **TX (notify)**: telemetria JSON periódica (2–5 s):
    `{hora_real, hora_jogo, dia, fase, clima_real, clima_jogo, temp, umid,
     comida, material, pop, ninhada, humor/energia/fome da rainha,
     alarmes, brilho, led, heap, uptime, ssid, cidade}`;
  - `...03` **RX burst (write)**: configuração completa (SSID/senha/cidade/fuso).
- MTU 247 + fragmentação simples (`{seq,n,chunk}`) para payloads > 200 B.
- Coexistência BLE+WiFi no ESP32 é suportada (coex SW); o BLE fica em
  prioridade baixa e só notifica a cada alguns segundos — impacto mínimo.
- Sem BLE disponível (iOS antigo etc.), fallback: o mesmo JSON por HTTP
  (`GET /api/state`, `POST /api/cmd`) quando a placa e o aparelho estão na
  mesma rede — o httpd já existe, basta registrar mais 2 handlers.

### 3.2 A PWA (static, sem servidor próprio)

- `tools/pwa/`: `index.html` + `app.js` + `manifest.webmanifest` + SW offline;
  o usuário abre uma vez (hospedado em GitHub Pages ou servido pela própria
  placa no modo AP) e instala (“Adicionar à tela inicial”).
- Conexão: `navigator.bluetooth.requestDevice({filters:[{namePrefix:'Formigueiro'}]})`
  → GATT → bind dos JSONs em cards (rainha, colônia, clima, relógios) +
  botões de comando (os mesmos `app_cmd_t` da tela).
- Estado offline do app: último snapshot em `localStorage`; reconexão automática.

### 3.3 Etapas de implementação (ordem sugerida)

1. `main/ble/ble_prov.c`: NimBLE GAP/GATT + fila de comandos → `g_cmd_queue`
   (reaproveita 100% dos comandos existentes); telemetria montada no `net_task`.
2. Handlers HTTP `/api/*` espelhando o mesmo JSON (fallback LAN).
3. PWA estática + testes com Chrome/Android.
4. (opcional) mDNS (`formigueiro.local`) para achar a placa na LAN.

Nada disso quebra o fluxo atual: BLE é adicional e opcional (sdkconfig já
teria `CONFIG_BT_ENABLED=y` + NimBLE; ~40–60 KB de RAM a mais — cabe hoje:
DRAM estático em 28%).

## 4. Tabela de soluções de problemas de rede

| Sintoma | Causa/ação |
|---|---|
| Portal não abre sozinho | Android: aviso de captive portal; iOS: “Entrar na rede”; ou abra 192.168.4.1 |
| Crash ao tocar WIFI 2× | era o netif duplicado — corrigido na v0.3.0 |
| Conecta mas hora errada | fuso: padrão `<-03>3`; ajuste em `nvs_config`/portal |
| Clima nunca muda | cidade sem geocode → veja ticker; corrija em CIDADE |
| Quero zerar credenciais | `idf.py erase-flash` (apaga NVS toda) |
