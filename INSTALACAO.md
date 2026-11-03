# Instalação e primeiros passos — Formigueiro (CYD)

Guia rápido para compilar e gravar o jogo numa placa **ESP32-2432S028R**
("Cheap Yellow Display"). Projeto 100% ESP-IDF (não usa Arduino/PlatformIO).

---

## 1. Instalar o ambiente (VS Code + ESP-IDF v5.5.1)

### Windows (recomendado)

1. Instale o **VS Code**: https://code.visualstudio.com/
2. Na aba de extensões, instale **"Espressif IDF"** (editor: Espressif Systems).
3. Abra a paleta de comandos (`Ctrl+Shift+P`) → **"ESP-IDF: Configure
   ESP-IDF Extension"** → aba **EXPRESS** → em *ESP-IDF Version* escolha
   **v5.5.1** → **Install**. (Baixa ~1,5 GB; precisa de Python, que o
   instalador providencia.)
4. Driver USB: a maioria dos CYD usa o chip **CH340**. Se a porta serial não
   aparecer no Gerenciador de Dispositivos ao conectar a placa, instale o
   driver CH340 (WCH) ou CP210x (Silicon Labs), conforme a revisão da placa.

### Linux / macOS

```bash
# dependências (Debian/Ubuntu)
sudo apt-get install git wget flex bison gperf python3 python3-pip python3-venv \
  cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0

git clone --depth 1 -b v5.5.1 --recurse-submodules --shallow-submodules \
  https://github.com/espressif/esp-idf.git ~/esp/esp-idf
cd ~/esp/esp-idf && ./install.sh esp32
```

A extensão "Espressif IDF" do VS Code também funciona no Linux/macOS — use
"ESP-IDF: Configure ESP-IDF Extension" como no Windows, se preferir.

---

## 2. Abrir o projeto

Descompacte o `.zip` e abra a pasta **`formigueiro/`** no VS Code
(*File → Open Folder*). É importante abrir a pasta do projeto (a que contém o
`CMakeLists.txt` raiz), não uma pasta acima.

---

## 3. Compilar

### Pelo VS Code

1. Na barra de status (rodapé), confira/selecione o target **esp32**. Se não
   aparecer: `Ctrl+Shift+P` → **"ESP-IDF: Set Espressif Device Target"** →
   **ESP32**.
2. Clique no ícone 🔨 (**Build**) da barra de status — ou `Ctrl+Shift+P` →
   **"ESP-IDF: Build your project"**.
3. A primeira compilação leva ~2-4 min. Ao final deve aparecer algo como:
   `formigueiro.bin binary size 0x10c0e0 bytes ... 58% free`.

### Pelo terminal

```bash
cd formigueiro

# Linux/macOS (ajuste o caminho se instalou em outro lugar):
. ~/esp/esp-idf/export.sh
# Windows (Prompt "ESP-IDF 5.5 PowerShell" criado pelo instalador já vem pronto)

idf.py set-target esp32    # só na primeira vez (apaga sdkconfig antigo, se houver)
idf.py build
```

---

## 4. Gravar na placa

1. Conecte o CYD ao PC pelo cabo USB (o conector **USB** da placa; o conector
   "CN1/UART" é opcional).
2. Descubra a porta: Windows → `COM3`, `COM5`… (Gerenciador de Dispositivos);
   Linux → `/dev/ttyUSB0` ou `/dev/ttyACM0`; macOS → `/dev/cu.usbserial-*`.
3. Grave:
   - **VS Code**: ícone ⚡ (**Flash**) na barra de status — ou `Ctrl+Shift+P`
     → "ESP-IDF: Flash your project". Selecione a porta quando perguntar.
   - **Terminal**: `idf.py -p COM5 flash` (ou `-p /dev/ttyUSB0`).
4. Para ver os logs: ícone 🖥️ (**Monitor**) ou `idf.py -p COM5 monitor`
   (sai com `Ctrl+]`).

> Dica: se a gravação falhar com "Failed to connect", segure o botão **BOOT**
> (RST/BOOT do CYD) enquanto o esptool tenta conectar, ou use
> `idf.py -p PORTA flash` duas vezes seguidas.

---

## 5. Primeiro boot — iniciar o jogo

1. **Calibração do touch**: toque exatamente nos 4 cantos indicados (dá para
   pular; refaz depois em Configurações → TOUCH).
2. **Orientação da tela**: escolha como o aparelho fica apoiado (refaz em
   Configurações → TELA).
3. O jogo começa no **dia 1, 08:00**, com a rainha na câmara real.

### WiFi, relógio e clima real (opcional, recomendado)

1. Sem rede configurada, a placa cria o hotspot **`Formigueiro-XXXX`**
   (senha **`formiga123`**; LED fica **ciano**).
2. Conecte o celular/PC nessa rede e abra **`http://192.168.4.1`**.
3. Informe o **SSID/senha** do seu WiFi e o **nome da sua cidade** → salvar.
4. A placa conecta (LED **azul piscando**), sincroniza a hora via SNTP
   (fuso padrão: Brasília) e passa a buscar o clima real na **Open-Meteo**
   a cada 20 min (sem chave de API). O LED fica **verde respirando** quando OK.

Sem WiFi o jogo funciona igual: relógio interno e clima simulado.

---

## 6. Controles básicos

- **Abas** 🏠/🌳 (canto inferior esquerdo) ou **swipe** na cena: interior ↔ exterior.
- **Botões rápidos**: DAR, BRIN, CAÇA, DORM, OBRA.
- **☰** ou **pressão longa** na cena: menu completo (carinho, alimentar, banho,
  ampliar câmara, por ovos…).
- **Toque na rainha**: carinho direto. **Toque numa operária/câmara/recurso**:
  inspeciona ou interage (a Lixeira despacha faxina; recursos no exterior
  mandam operárias coletar).
- **Configurações**: velocidade do jogo (x1/x2/x4/pausa), brilho, cidade,
  clima real on/off, hora, calibração, salvar, recomeçar colônia (toque duplo).

Detalhes completos do gameplay, LED RGB e economia da colônia: `README.md`.

---

## 7. Problemas comuns

| Sintoma | Causa provável / solução |
|---|---|
| Porta serial não aparece | Driver CH340/CP210x não instalado (seção 1) |
| "Failed to connect to ESP32" | Segure BOOT durante o reset; ou cabo só de carga (troque o cabo) |
| Tela toda branca/escura | Backlight ou MADCTL de clone — ver `main/app_config.h` (LCD_MADCTL) |
| Cores invertidas / azul↔vermelho | Clone com modo diferente: troque o MADCTL em `app_config.h` |
| Toque desalinhado | Refaça a calibração (Configurações → TOUCH) |
| `idf.py` não encontrado no terminal | Rode `. ~/esp/esp-idf/export.sh` (Linux/macOS) ou use o prompt "ESP-IDF" do Windows |
| Clima nunca atualiza | Cidade não encontrada/geocode falhou — ajuste o nome em Configurações → CIDADE |

---

## 8. Apagar tudo (reset de fábrica do jogo)

Para zerar save + configurações: Configurações → **COLONIA** (toque duplo)
recomeça a colônia; para limpar o flash inteiro:

```bash
idf.py -p PORTA erase-flash
idf.py -p PORTA flash monitor
```
