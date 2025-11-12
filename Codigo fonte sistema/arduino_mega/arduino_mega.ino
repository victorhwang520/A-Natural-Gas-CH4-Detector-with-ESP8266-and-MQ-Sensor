// ────────────────────────────────────────────────
// Arduino Mega 2560 + MQ-5
//
// MQ-5:
//   AO  -> A0
//   DO  -> 52
//   VCC -> 5V
//   GND -> GND
//
// Comunicação com ESP8266:
//   TX3 (pino 14) -> divisor 10k/20k -> RXD0 (GPIO3) do ESP
//   GND em comum
// ────────────────────────────────────────────────

#include <math.h>

const int MQ_A0_PIN = A0;
const int MQ_D0_PIN = 52;

// Parâmetros elétricos do módulo
const float MQ_VC = 5.0;        // tensão de alimentação
const float MQ_RL = 20000.0;    // resistor de carga (20 kΩ)

// Calibração (R0)
float MQ_R0 = 10000.0;

// Curva aproximada do MQ-5 (LPG / Gás combustível)
// log10(ppm) = (log10(Rs/R0) - B) / A
const float MQ_A = -0.3973;
const float MQ_B =  0.7472;

// Filtro exponencial
float ratioFiltered = 0.0;
float ppmFiltered   = 0.0;
const float ALPHA   = 0.2;

// ───── Funções auxiliares ─────

// Leitura do ADC com média
int readMQadcAveraged(int samples = 20) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(MQ_A0_PIN);
    delay(5);
  }
  return (int)(sum / samples);
}

// ADC (0..1023) -> tensão em Rs (0..5 V)
float mq5_getVrs(int adc) {
  return adc * (5.0 / 1023.0);
}

// *** NOVA fórmula de Rs ***
// Medindo tensão NO SENSOR (Rs):
// Vrs = Vc * Rs / (Rs + RL)  =>  Rs = (Vrs * RL) / (Vc - Vrs)
float mq5_getRs(float vrs) {
  if (vrs <= 0.0 || vrs >= MQ_VC) return 1e9;
  float rs = (vrs * MQ_RL) / (MQ_VC - vrs);
  return rs;
}

// Rs/R0 -> ppm usando curva log-log
float mq5_getPPM(float rs_over_r0) {
  if (rs_over_r0 <= 0.0) return 0.0;
  float logRatio = log10(rs_over_r0);
  float logPPM   = (logRatio - MQ_B) / MQ_A;
  float ppm      = pow(10.0, logPPM);
  return ppm;
}

// Calibração em ar limpo (sem gás forte por perto)
void mq5_calibrate() {
  Serial.println(F("=== Calibrando MQ-5 em ar limpo (10 s) ==="));
  unsigned long start = millis();
  unsigned long count = 0;
  double sumRs = 0.0;

  while (millis() - start < 10000) {
    int   adc = readMQadcAveraged(10);
    float vrs = mq5_getVrs(adc);
    float rs  = mq5_getRs(vrs);

    sumRs += rs;
    count++;
  }

  float rsAir = (float)(sumRs / count);

  // Datasheet: Rs(ar)/R0 ≈ 5  =>  R0 ≈ Rs(ar)/5
  MQ_R0 = rsAir / 5.0;

  Serial.print(F("Calibração concluída. R0 ≈ "));
  Serial.print(MQ_R0, 1);
  Serial.println(F(" ohms"));
  Serial.println(F("───────────────────────────────────────────────"));
}

// ───── SETUP ─────
void setup() {
  Serial.begin(9600);    // debug via USB
  Serial3.begin(9600);   // comunicação com ESP

  pinMode(MQ_A0_PIN, INPUT);
  pinMode(MQ_D0_PIN, INPUT);

  Serial.println(F("MQ-5 + ppm + envio para ESP8266 (Serial3 -> RXD0)"));
  Serial.println(F("Aguardando calibracao..."));
  mq5_calibrate();
}

// ───── LOOP ─────
void loop() {
  // 1) Leitura analógica
  int   adc = readMQadcAveraged(20);
  float vrs = mq5_getVrs(adc);
  float rs  = mq5_getRs(vrs);

  // 2) Razão e ppm
  float ratio = rs / MQ_R0;
  float ppm   = mq5_getPPM(ratio);

  // 3) Filtro exponencial
  static bool first = true;
  if (first) {
    ratioFiltered = ratio;
    ppmFiltered   = ppm;
    first = false;
  } else {
    ratioFiltered = ALPHA * ratio + (1.0 - ALPHA) * ratioFiltered;
    ppmFiltered   = ALPHA * ppm   + (1.0 - ALPHA) * ppmFiltered;
  }

  // 4) Saída digital
  int d0 = digitalRead(MQ_D0_PIN);

  // 5) Debug no PC
  Serial.print(F("ADC="));  Serial.print(adc);
  Serial.print(F(" | Vrs="));  Serial.print(vrs, 3);
  Serial.print(F(" V | Rs="));  Serial.print(rs, 0);
  Serial.print(F(" | Rs/R0=")); Serial.print(ratioFiltered, 3);
  Serial.print(F(" | ppm="));   Serial.print(ppmFiltered, 1);
  Serial.print(F(" | D0="));    Serial.println(d0);

  // 6) Envia linha pro ESP (Serial3 -> RXD0)
  // Formato:
  //   ADC:123;VRS:1.234;RS:12345;RATIO:6.789;PPM:12.3;D0:0
  Serial3.print("ADC:");
  Serial3.print(adc);
  Serial3.print(";VRS:");
  Serial3.print(vrs, 3);
  Serial3.print(";RS:");
  Serial3.print(rs, 0);
  Serial3.print(";RATIO:");
  Serial3.print(ratioFiltered, 3);
  Serial3.print(";PPM:");
  Serial3.print(ppmFiltered, 1);
  Serial3.print(";D0:");
  Serial3.print(d0);
  Serial3.print("\n");

  delay(1000);
}
