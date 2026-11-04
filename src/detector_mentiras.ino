// DETECTOR DE MENTIRAS (demonstracao) - versao 9 (START/STOP + REINICIAR)
// Arduino Uno + KY-039 + 3 LEDs + OLED SSD1306 128x64 (I2C) + 2 botoes
//
// PINOS
//   Sensor KY-039 ......... A0
//   LED verde ............. D8   (resistor 220 ohm)
//   LED amarelo ........... D9   (resistor 220 ohm)
//   LED vermelho .......... D10  (resistor 220 ohm)
//   Botao REINICIAR ....... D2 e GND (pull-up interno, sem resistor)
//   Botao START/STOP ...... D3 e GND (pull-up interno, sem resistor)
//   OLED SDA .............. A4
//   OLED SCL .............. A5
//
// FUNCIONAMENTO
//   Ao ligar o Arduino fica DESLIGADO (standby): LEDs apagados.
//   START/STOP (D3):
//        desligado -> inicia o teste
//        teste rodando (ou resultado na tela) -> desliga
//   REINICIAR (D2): recomeca o teste do zero (so com o teste ligado)
//
//   Teste: amarelo acende -> media 1 (10 s) -> amarelo pisca 3x
//   -> "FACA A PERGUNTA" -> media 2 (15 s) -> 3 LEDs + "ANALISANDO..."
//   -> fica so o LED do resultado:
//        media 2 > media 1  = VERMELHO (mentira)
//        media 2 <= media 1 = VERDE (verdade)
//
// BIBLIOTECAS: "Adafruit SSD1306" e "Adafruit GFX Library"
//
// SERIAL (9600 baud)
//   MODO_PLOTTER = true  -> abra o Serial Plotter (grafico do pulso)
//   MODO_PLOTTER = false -> abra o Serial Monitor (textos)

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const bool MODO_PLOTTER = true;  // true = Serial Plotter; false = Serial Monitor

// ---------- Pinos ----------
const int PIN_SENSOR   = A0;
const int LED_VERDE    = 8;
const int LED_AMARELO  = 9;
const int LED_VERMELHO = 10;
const int PIN_REINICIAR = 2;
const int PIN_START     = 3;

// ---------- OLED ----------
const uint8_t OLED_ENDERECO = 0x3C;   // se nao acender, tente 0x3D
Adafruit_SSD1306 display(128, 64, &Wire, -1);
bool temTela = false;
const bool TELA_APAGADA_NO_STANDBY = false; // true = tela totalmente apagada quando desligado

// ---------- Ajustes ----------
const unsigned long DURACAO_MEDIA1_MS = 15000;
const unsigned long DURACAO_MEDIA2_MS = 15000;
const unsigned long TEMPO_SUSPENSE_MS = 3000;
const float MARGEM_BPM = 3;            // 0 = qualquer aumento vira mentira (teste 1 a 3)
const int   MIN_BATIDAS = 6;
const bool  MOSTRAR_BPM_FASE2 = false; // false = esconde o BPM durante a media 2 (suspense)
const bool  DIAGNOSTICO = true;        // (modo Monitor) mostra leitura/amp/batidas a cada 1 s

// ---------- Ajustes do sinal ----------
const unsigned long PERIODO_US = 5000;        // 200 Hz
const unsigned long AQUECIMENTO_MS = 1500;
const int   OVERSAMPLE = 8;
const float LIMIAR_MIN = 1.5;
const float SPIKE_MIN = 120.0;
const unsigned long BLOQUEIO_MS = 1000;
const unsigned long REFRATARIO_MS = 500;   // evita dupla contagem da mesma batida
const unsigned long REARME_FORCADO_MS = 1800;
const float LIMIAR_FATOR = 0.55;
const float DESCIDA_FATOR = 0.55;
const float MIN_AMPLITUDE_BATIDA = 3.0;
const unsigned long SEM_DEDO_MS = 5000;

// ---------- Estado ----------
enum Fase { ESPERA, MEDINDO_1, MEDINDO_2, FIM };
Fase fase = ESPERA;

float filtro = 0, base = 0;
float ampMedia = 0, picoCiclo = 0;
bool armado = true;
bool candidatoBatida = false;
float picoCandidato = 0;
unsigned long tCandidato = 0;
unsigned long ultimaAmostra = 0, tLiberar = 0, tDiag = 0, tUltimaTela = 0;
unsigned long tUltimoPico = 0, tUltimaBatidaReal = 0;
int contadorPlot = 0;

const int MAX_INTERVALOS = 50;
uint16_t intervalos[MAX_INTERVALOS];
int n = 0;
unsigned long ultimaBatida = 0;
unsigned long inicioJanela = 0;   // 0 = ainda nao comecou
float media1 = 0;
float bpmAtual = 0;
bool batidaPar = false;

// ---------- Botoes ----------
// indice 0 = REINICIAR, indice 1 = START/STOP
// (sem struct: a IDE do Arduino cria prototipos automaticos que quebram com struct)
const int PINOS_BOTOES[2] = { PIN_REINICIAR, PIN_START };
bool botaoAnterior[2] = { HIGH, HIGH };
unsigned long botaoTempo[2] = { 0, 0 };

// =====================================================
//                    SERIAL
// =====================================================
void texto(const __FlashStringHelper* s) {
  if (!MODO_PLOTTER) Serial.println(s);
}

void textoNum(const __FlashStringHelper* s, float v, int casas, const __FlashStringHelper* fim) {
  if (MODO_PLOTTER) return;
  Serial.print(s);
  Serial.print(v, casas);
  Serial.println(fim);
}

void plota(float sinal, float limiar, unsigned long agora) {
  if (!MODO_PLOTTER) return;

  // Serial Plotter: cada linha = uma amostra.
  // Series: Sinal, Limite, Batida
  if (++contadorPlot < 4) return;   // ~50 Hz
  contadorPlot = 0;

  float s = constrain(sinal, -40.0f, 40.0f);
  float l = constrain(limiar, 0.0f, 40.0f);
  int batida = (agora - tUltimaBatidaReal < 120) ? 10 : 0;

  Serial.print(s, 2);
  Serial.print(',');
  Serial.print(l, 2);
  Serial.print(',');
  Serial.println(batida);
}

// =====================================================
//                    BOTOES
// =====================================================
bool pressionado(int i) {
  bool l = digitalRead(PINOS_BOTOES[i]);
  if (l != botaoAnterior[i] && millis() - botaoTempo[i] > 50) {
    botaoTempo[i] = millis();
    botaoAnterior[i] = l;
    if (l == LOW) return true;
  }
  return false;
}

// 0 = nada, 1 = REINICIAR, 2 = START/STOP
int leBotoes() {
  bool s = pressionado(1);   // START/STOP
  bool r = pressionado(0);   // REINICIAR
  if (s) return 2;
  if (r) return 1;
  return 0;
}

// Espera ms milissegundos; devolve o botao apertado (0 = nenhum)
int esperaOuBotao(unsigned long ms) {
  unsigned long ini = millis();
  while (millis() - ini < ms) {
    int b = leBotoes();
    if (b) return b;
    delay(5);
  }
  return 0;
}

// =====================================================
//                    OLED
// =====================================================
void centralizaF(const __FlashStringHelper* txt, int y, int tam) {
  int16_t x1, y1; uint16_t w, h;
  display.setTextSize(tam);
  display.getTextBounds(txt, 0, y, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, y);
  display.print(txt);
}

void centraliza(const char* txt, int y, int tam) {
  int16_t x1, y1; uint16_t w, h;
  display.setTextSize(tam);
  display.getTextBounds(txt, 0, y, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, y);
  display.print(txt);
}

void coracao(int x, int y, bool cheio) {
  if (cheio) {
    display.fillCircle(x - 3, y + 2, 3, WHITE);
    display.fillCircle(x + 3, y + 2, 3, WHITE);
    display.fillTriangle(x - 6, y + 3, x + 6, y + 3, x, y + 10, WHITE);
  } else {
    display.drawCircle(x - 3, y + 2, 3, WHITE);
    display.drawCircle(x + 3, y + 2, 3, WHITE);
    display.drawTriangle(x - 6, y + 3, x + 6, y + 3, x, y + 10, WHITE);
  }
}

void telaLigada(bool ligada) {
  if (!temTela) return;
  display.ssd1306_command(ligada ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
}

void telaEspera() {
  if (!temTela) return;
  if (TELA_APAGADA_NO_STANDBY) {
    display.clearDisplay();
    display.display();
    telaLigada(false);
    return;
  }
  telaLigada(true);
  display.clearDisplay();
  display.setTextColor(WHITE);
  centralizaF(F("DETECTOR"), 6, 2);
  centralizaF(F("de mentiras"), 26, 1);
  centralizaF(F("DESLIGADO"), 40, 1);
  centralizaF(F("Aperte START"), 52, 1);
  display.display();
}

void telaAbertura() {
  if (!temTela) return;
  display.clearDisplay();
  display.setTextColor(WHITE);
  centralizaF(F("DETECTOR"), 8, 2);
  centralizaF(F("de mentiras"), 30, 1);
  centralizaF(F("Feira de Ciencias"), 48, 1);
  display.display();
}

void desenhaMedindo() {
  if (!temTela) return;
  unsigned long agora = millis();
  unsigned long dur = (fase == MEDINDO_1) ? DURACAO_MEDIA1_MS : DURACAO_MEDIA2_MS;
  bool aoVivo = (fase == MEDINDO_1) || MOSTRAR_BPM_FASE2;

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  if (fase == MEDINDO_1) display.print(F("1) MEDIA BASE"));
  else                   display.print(F("2) RESPOSTA"));

  char buf[12];

  if (inicioJanela != 0) {
    unsigned long dec = agora - inicioJanela;
    int rest = (dec >= dur) ? 0 : (int)((dur - dec) / 1000 + 1);
    sprintf(buf, "%ds", rest);
    display.setTextSize(1);
    display.setCursor(128 - 6 * strlen(buf), 0);
    display.print(buf);
  }

  if (ultimaBatida != 0) coracao(96, 0, batidaPar);

  if (inicioJanela == 0) {
    centralizaF(F("Coloque o dedo"), 24, 1);
    centralizaF(F("no sensor"), 36, 1);
  } else if (aoVivo && bpmAtual > 0) {
    sprintf(buf, "%d", (int)(bpmAtual + 0.5));
    centraliza(buf, 16, 3);
    centralizaF(F("BPM"), 42, 1);
  } else {
    centralizaF(F("Medindo..."), 22, 2);
  }

  if (inicioJanela != 0) {
    unsigned long dec = agora - inicioJanela;
    int w = (dec >= dur) ? 124 : (int)(124UL * dec / dur);
    display.drawRect(0, 54, 128, 10, WHITE);
    display.fillRect(2, 56, w, 6, WHITE);
  }
  display.display();
}

void telaPergunta() {
  if (!temTela) return;
  display.clearDisplay();
  display.setTextColor(WHITE);
  centralizaF(F("FACA A"), 4, 2);
  centralizaF(F("PERGUNTA!"), 24, 2);
  char buf[24];
  sprintf(buf, "Base: %d BPM", (int)(media1 + 0.5));
  centraliza(buf, 52, 1);
  display.display();
}

void telaAnalisando(int pontos) {
  if (!temTela) return;
  display.clearDisplay();
  display.setTextColor(WHITE);
  centralizaF(F("ANALISANDO"), 14, 2);
  display.setTextSize(2);
  display.setCursor(40, 38);
  for (int i = 0; i < pontos; i++) display.print('.');
  display.display();
}

void telaResultado(bool mentira, float m1, float m2) {
  if (!temTela) return;
  display.clearDisplay();
  if (mentira) {
    display.fillRect(0, 0, 128, 64, WHITE);
    display.setTextColor(BLACK);
  } else {
    display.setTextColor(WHITE);
  }
  if (mentira) centralizaF(F("MENTIRA!"), 6, 2);
  else         centralizaF(F("VERDADE"), 6, 2);

  char buf[24];
  sprintf(buf, "Base:  %d BPM", (int)(m1 + 0.5));
  centraliza(buf, 30, 1);
  sprintf(buf, "Agora: %d BPM", (int)(m2 + 0.5));
  centraliza(buf, 42, 1);
  centralizaF(F("Reiniciar = novo"), 54, 1);
  display.display();
}

// Atualiza a tela so em momentos "seguros", para nao atrasar a leitura de uma batida
void atualizaTela(unsigned long agora) {
  if (!temTela || fase == FIM || fase == ESPERA) return;
  if (agora - tUltimaTela < 400) return;
  unsigned long dp = agora - tUltimoPico;
  bool seguro = (dp > 80 && dp < 250) || dp > 1500;
  if (!seguro) return;
  tUltimaTela = agora;
  desenhaMedindo();
}

// =====================================================
//                    SETUP / LOOP
// =====================================================
void setup() {
  Serial.begin(9600);
  delay(200);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_AMARELO, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  pinMode(PIN_REINICIAR, INPUT_PULLUP);
  pinMode(PIN_START, INPUT_PULLUP);

  temTela = display.begin(SSD1306_SWITCHCAPVCC, OLED_ENDERECO);
  if (!temTela) {
    texto(F("OLED nao encontrado: confira SDA/SCL e o endereco (0x3C ou 0x3D)."));
  }

  texto(F("=== DETECTOR DE MENTIRAS ==="));
  telaAbertura();
  delay(1500);

  desliga();   // comeca desligado, esperando o botao START
}

void loop() {
  int b = leBotoes();
  if (b) { trataAcao(b); return; }

  if (fase == ESPERA || fase == FIM) return;

  unsigned long t = micros();
  if (t - ultimaAmostra < PERIODO_US) return;
  ultimaAmostra += PERIODO_US;
  if (t - ultimaAmostra > 20000) ultimaAmostra = t;

  // ----- Leitura com sobreamostragem -----
  long soma = 0;
  for (int i = 0; i < OVERSAMPLE; i++) soma += analogRead(PIN_SENSOR);
  float leitura = (float)soma / OVERSAMPLE;

  // ----- Filtros -----
  filtro += 0.25 * (leitura - filtro);
  base   += 0.01 * (filtro - base);
  float sinal = filtro - base;

  unsigned long agora = millis();

  // ----- Diagnostico (modo Monitor) -----
  if (DIAGNOSTICO && !MODO_PLOTTER && agora - tDiag >= 1000) {
    tDiag = agora;
    Serial.print(F("[leitura="));
    Serial.print(leitura, 0);
    Serial.print(F(" amp="));
    Serial.print(ampMedia, 1);
    Serial.print(F(" batidas="));
    Serial.print(n);
    Serial.println(F("]"));
  }

  // ----- Mudanca brusca: recentraliza a base e espera um pouco -----
  float limiteSpike = max(SPIKE_MIN, 5.0f * ampMedia);
  if (fabs(sinal) > limiteSpike) {
    tLiberar = agora + BLOQUEIO_MS;
    ultimaBatida = 0;
  }
  if (agora < tLiberar) {
    base = filtro;
    ampMedia = 0;
    picoCiclo = 0;
    armado = true;
    plota(0, LIMIAR_MIN, agora);
    return;
  }

  // ----- Deteccao robusta da batida -----
  // Exige uma onda completa: subida -> pico -> descida.
  // Isso evita que pequenas oscilações do KY-039 sejam contadas como batimentos.
  float limiar = max(LIMIAR_MIN, LIMIAR_FATOR * ampMedia);

  if (!candidatoBatida) {
    ampMedia *= 0.9997;

    if (sinal > limiar &&
        (tUltimoPico == 0 || (agora - tUltimoPico) > REFRATARIO_MS)) {

      candidatoBatida = true;
      picoCandidato = sinal;
      picoCiclo = sinal;
      tCandidato = agora;
    }
  } else {
    // Guarda o verdadeiro pico da onda.
    if (sinal > picoCandidato) {
      picoCandidato = sinal;
      picoCiclo = sinal;
    }

    // Só confirma quando a onda começa a descer.
    float limiarDescida = max(LIMIAR_MIN * 0.70,
                              DESCIDA_FATOR * limiar);

    bool terminouOnda = (sinal < limiarDescida);
    bool demorouDemais = (agora - tCandidato) > REARME_FORCADO_MS;

    if (terminouOnda || demorouDemais) {
      float pico = picoCandidato;

      if (pico >= MIN_AMPLITUDE_BATIDA) {
        // A amplitude de referência aprende apenas ondas confirmadas.
        ampMedia = (ampMedia == 0)
                     ? pico
                     : (0.80 * ampMedia + 0.20 * pico);

        if (tUltimoPico == 0 ||
            (agora - tUltimoPico) >= REFRATARIO_MS) {
          tUltimoPico = agora;
          registraBatida(agora);
        }
      }

      candidatoBatida = false;
      picoCandidato = 0;
      tCandidato = 0;
      armado = true;
    }
  }

  plota(sinal, limiar, agora);
  atualizaTela(agora);

  // ----- Dedo saiu (sem batidas por 5 s): recomeca a janela -----
  if (inicioJanela != 0 && (agora - tUltimaBatidaReal) > SEM_DEDO_MS) {
    texto(F("Sem batidas, reiniciando a medicao..."));
    iniciaMedicao();
  }

  // ----- Fim da janela (espera ter batidas suficientes) -----
  unsigned long duracao = (fase == MEDINDO_1) ? DURACAO_MEDIA1_MS : DURACAO_MEDIA2_MS;
  if (inicioJanela != 0 && (agora - inicioJanela) >= duracao && n >= MIN_BATIDAS) {
    finalizaMedicao();
  }
}

// =====================================================
//                    CONTROLE (START / STOP / REINICIAR)
// =====================================================
// acao: 1 = REINICIAR, 2 = START/STOP
void trataAcao(int acao) {
  if (acao == 2) {
    if (fase == ESPERA) reiniciaTudo();   // estava desligado: liga
    else                desliga();        // estava ligado: desliga
  } else if (acao == 1) {
    if (fase != ESPERA) reiniciaTudo();   // reiniciar so vale com o teste ligado
  }
}

// Desliga o teste: LEDs apagados, sensor parado, aguarda START
void desliga() {
  apagaLeds();
  fase = ESPERA;
  iniciaMedicao();
  texto(F("--- DESLIGADO. Aperte START ---"));
  telaEspera();
}

// Liga/recomeca o teste do zero
void reiniciaTudo() {
  telaLigada(true);
  apagaLeds();
  digitalWrite(LED_AMARELO, HIGH);

  // Recalibra a leitura inicial do sensor
  long soma = 0;
  for (int i = 0; i < 30; i++) { soma += analogRead(PIN_SENSOR); delay(2); }
  filtro = base = soma / 30.0;

  fase = MEDINDO_1;
  media1 = 0;
  iniciaMedicao();
  ampMedia = 0;
  picoCiclo = 0;
  armado = true;
  tLiberar = millis() + AQUECIMENTO_MS;
  tUltimoPico = 0;
  tUltimaBatidaReal = 0;
  tUltimaTela = millis();
  ultimaAmostra = micros();

  texto(F("--- Teste iniciado ---"));
  texto(F("Coloque o dedo no sensor (parado, pressao leve)."));
  texto(F("Medindo a media 1 (10 s apos a primeira batida)..."));
  desenhaMedindo();
}

// =====================================================
//                    MEDICAO
// =====================================================
void iniciaMedicao() {
  n = 0;
  ultimaBatida = 0;
  inicioJanela = 0;
  bpmAtual = 0;
  candidatoBatida = false;
  picoCandidato = 0;
  tCandidato = 0;
}

void registraBatida(unsigned long agora) {
  if (inicioJanela == 0) inicioJanela = agora;
  tUltimaBatidaReal = agora;
  batidaPar = !batidaPar;

  if (ultimaBatida != 0) {
    unsigned long iv = agora - ultimaBatida;
    if (iv >= 500 && iv <= 1500) {              // 40 a 120 BPM; foco em repouso
      if (n < MAX_INTERVALOS) intervalos[n++] = (uint16_t)iv;
      float bpm = 60000.0 / iv;

      // Mostra uma estimativa suavizada, mas nunca deixa um pico isolado
      // dominar o valor exibido.
      bpmAtual = (bpmAtual == 0) ? bpm : (0.85 * bpmAtual + 0.15 * bpm);

      // Rejeita valores fisiologicamente improváveis para o modo de repouso.
      if (bpmAtual < 40) bpmAtual = 40;
      if (bpmAtual > 120) bpmAtual = 120;

      textoNum(F("BPM: "), bpmAtual, 1, F(""));
    }
  }
  ultimaBatida = agora;
}

// Media dos intervalos, descartando os que fogem da mediana
float calculaMediaBpm() {
  uint16_t ord[MAX_INTERVALOS];
  for (int i = 0; i < n; i++) ord[i] = intervalos[i];
  for (int i = 1; i < n; i++) {
    uint16_t v = ord[i];
    int j = i - 1;
    while (j >= 0 && ord[j] > v) { ord[j + 1] = ord[j]; j--; }
    ord[j + 1] = v;
  }
  float mediana = (n % 2) ? ord[n / 2] : (ord[n / 2 - 1] + ord[n / 2]) / 2.0;

  float soma = 0;
  int ok = 0;
  for (int i = 0; i < n; i++) {
    if (intervalos[i] > mediana * 0.75 && intervalos[i] < mediana * 1.25) {
      soma += intervalos[i];
      ok++;
    }
  }
  if (ok < 3) return 0;

  float bpmFinal = 60000.0 / (soma / ok);

  // Não força o resultado para 80–100 BPM.
  // Se o sensor realmente detectar outro valor, ele deve aparecer.
  return bpmFinal;
}

void finalizaMedicao() {
  float bpm = calculaMediaBpm();

  if (bpm == 0) {
    texto(F("Batidas irregulares, reiniciando a medicao..."));
    iniciaMedicao();
    return;
  }

  if (fase == MEDINDO_1) {
    media1 = bpm;
    textoNum(F(">>> MEDIA 1: "), media1, 1, F(" BPM"));
    texto(F("Faca a pergunta! Medindo a media 2 (15 s)..."));

    telaPergunta();
    int a = piscaAmarelo3x();
    if (a) { trataAcao(a); return; }

    fase = MEDINDO_2;
    iniciaMedicao();
    ampMedia = 0;
    armado = true;
    tLiberar = millis() + 500;
    tUltimaTela = 0;
    ultimaAmostra = micros();
    desenhaMedindo();
  }
  else {
    textoNum(F(">>> MEDIA 2: "), bpm, 1, F(" BPM"));

    // Suspense: 3 LEDs ligados antes do resultado
    digitalWrite(LED_VERDE, HIGH);
    digitalWrite(LED_AMARELO, HIGH);
    digitalWrite(LED_VERMELHO, HIGH);
    int a = suspense();
    if (a) { trataAcao(a); return; }
    apagaLeds();

    bool mentira = (bpm > media1 + MARGEM_BPM);
    if (mentira) {
      digitalWrite(LED_VERMELHO, HIGH);
      texto(F("RESULTADO: MENTIRA (vermelho)"));
    } else {
      digitalWrite(LED_VERDE, HIGH);
      texto(F("RESULTADO: VERDADE (verde)"));
    }
    telaResultado(mentira, media1, bpm);
    fase = FIM;
  }
}

// =====================================================
//                    LEDS / EFEITOS
// =====================================================
// Devolve o botao apertado durante o efeito (0 = nenhum)
int piscaAmarelo3x() {
  int a;
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_AMARELO, LOW);
    a = esperaOuBotao(300);
    if (a) return a;
    digitalWrite(LED_AMARELO, HIGH);
    a = esperaOuBotao(300);
    if (a) return a;
  }
  digitalWrite(LED_AMARELO, LOW);
  return 0;
}

int suspense() {
  unsigned long ini = millis();
  int k = 0;
  while (millis() - ini < TEMPO_SUSPENSE_MS) {
    telaAnalisando(k % 4);
    k++;
    int a = esperaOuBotao(400);
    if (a) return a;
  }
  return 0;
}

void apagaLeds() {
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_AMARELO, LOW);
  digitalWrite(LED_VERMELHO, LOW);
}
