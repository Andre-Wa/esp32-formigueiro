# Manual do jogo — Formigueiro v0.3.0

Você cuida de uma colônia: a **rainha** é o seu pet; as **operárias** são NPCs
com rotina própria. Duas cenas: o **interior** (câmaras e túneis) e o
**exterior** (jardim coletável de 640 px). Um dia de jogo ≈ 80 min reais (x1).

## 1. Telas e controles

```
┌──────────────────────────────────────┐
│ status: hora·dia·clima·comida·material│  toque no ⚙ = Configurações;
│ cena (interior OU exterior)          │  toque no ⏱ = cicla velocidade
│ ticker de mensagens                  │
├──────────────────────────────────────┤
│ 🏠  │ DAR BRIN CAÇA DORM OBRA │ ☰ OVOS │
└──────────────────────────────────────┘
```

- **🏠 / 🌳**: interior ↔ exterior. No interior, um **arrasto horizontal**
  também troca; no **exterior, arrastar move a câmera** (1:1) — o auto-seguir
  de operárias pausa ~20 s para não brigar com você.
- **DAR/BRIN/CAÇA/DORM/OBRA**: alimentar, brincar, expedição de caça,
  descansar, iniciar obra na câmara selecionada.
- **☰ (ou pressão longa na cena)**: menu CARINHO, ALIMENTAR, BRINCAR, BANHO,
  DORMIR, CAÇAR, AMPLIAR, CUIDAR, COLETAR, POR OVOS, FECHAR.
- **OVOS**: pede postura (rainha alimentada ≥55, energia ≥25, berçário com vaga).
- **Toques diretos**: rainha = carinho (balões de fala); operária = inspeciona;
  câmara = seleciona + mostra info útil (estoques, umidade do jardim, ninhada,
  humor da rainha, higiene); **Lixeira** = despacha faxina; recurso externo =
  manda coletar; chão externo = manda batedora; **aranha** = mostra a vida dela.

## 2. As câmaras (interior)

| Câmara | Função | Ampliar (nv.1→2→3) melhora |
|---|---|---|
| Despensa | guarda comida/material | capacidade de comida |
| **Jardim de fungos** | produz comida sozinho: `2·nível +umidade +chuva` a cada 2 h | produção |
| Berçário | ovos→larvas→pupas→operárias | teto de ninhada (4/8/12) e velocidade |
| Câmara real | rainha; carinho/cuidados | humor/energia da rainha |
| Lixeira | higiene da colônia | sujeira acumula mais devagar |
| Átrio | cruzamento dos túneis | — |

**Operárias respeitam os túneis desenhados** (grafo de 8 nós, mesmos corredores
do render). Tocando numa câmara você vê nível, custo e estado; **AMPLIAR/OBRA**
cobra o material na hora (30 → 60), manda 2 operárias escavar **na própria
câmara** e avisa quando o nível sobe. Sem material? Colete **gravetos e
pedras** no exterior (eles viram material na despensa).

## 3. Rotina e necessidades

- Fases do dia: noite (21–5 h), alvorada, dia, tarde, entardecer — céu, luz e
  tarefas mudam. À noite quase todos dormem no berçário.
- Operárias: energia/fome/saúde. Energia cai devagar (v0.3.0) e recupera
  dormindo ou comendo. Com fome vão à despensa; exaustas, ao berçário.
- **Equipe externa** (Configurações → EQUIPE FORA): quantas operárias podem
  ficar na rua ao mesmo tempo (2/3/4/6/todas). É o seu “controle de grupo”:
  teto baixo = colônia descansada e obra andando; teto alto = coleta agressiva.
- Chuva: operárias presas lá fora esperam (`W_STUCK`); o jardim de fungos
  ganha umidade; tempestade = alarme roxo.
- Higiene: câmaras sujam; faxina (Lixeira) limpa. Sujeira alta derruba o humor.

## 4. Exterior

- Recursos: folhas, frutas, arbusto, flores, sementes, gravetos (material),
  cogumelos, pedras (material) e besouros (presa). Eles **renascem** com o tempo.
- Coleta: toque no recurso (ou COLETAR no menu) → operária livre vai, carrega
  e entrega na despensa (+comida/+material/+pontos).
- **Aranha** (x 470–620): embosca coletoras. Em v0.3.0 ela **tem vida**:
  operárias perto dela revidam automaticamente; ao derrotá-la você ganha
  +25 comida, e ela só volta ~2 dias de jogo depois. Tocá-la mostra a vida.
- Câmera: arraste para os lados; sem arrasto, ela segue a operária em missão.

## 5. Clima, relógio e LED

- Relógio real por SNTP (fuso padrão Brasília; ajustável em Configurações).
- Clima real via Open-Meteo quando o WiFi está configurado (cidade em
  Configurações → CIDADE); sem WiFi, um simulador coerente assume.
- LED RGB: verde respirando (dia ok), azul (noite), azul piscando (conectando),
  ciano (portal WiFi), âmbar/vermelho/roxo (alarmes fome/predador/tempestade),
  piscada amarela (eventos: ovo, obra, carinho, aranha morta).

## 6. Configurações (☰ na barra de status)

VELOCIDADE (x1/x2/x4/pausa) · **BRILHO** com botões − / + reais (passo 10,
limites 10–100, sem “voltar do nada”) e tecla **AUTO** (sensor de luz do CYD) ·
**EQUIPE FORA** · WIFI (abre/fecha o portal; ver `REDE.md`) · CIDADE ·
CLIMA REAL · HORA · TOUCH (calibrar) · TELA (orientação) · SAVE (salvar agora) ·
COLONIA (toque duplo = recomeçar) · diário da colônia.

## 7. Progressão e save

- Pontos por entregas/obras/ovos. Níveis de câmara aumentam tetos
  (operárias 8/14/20, ninhada 4/8/12, comida).
- Save automático em flash (partição `save`); ao religar, `colony_offline()`
  avança até 48 h com regras simplificadas (operárias descansam, fungo
  produz, ninhada anda, recursos renascem).

## 8. Dicas rápidas

1. Comece com EQUIPE FORA = 3 e mande coletar **gravetos/pedras** (material).
2. Amplie o **Jardim de fungos** cedo: é renda passiva de comida.
3. Berçário nv.2 + CUIDAR acelera novas operárias.
4. Mantenha higiene > 60 (toque na Lixeira de vez em quando).
5. Viu operária ferida perto da aranha? Deixe mais uma ou duas por perto —
   elas derrotam a aranha sozinhas e rendem +25 comida.
