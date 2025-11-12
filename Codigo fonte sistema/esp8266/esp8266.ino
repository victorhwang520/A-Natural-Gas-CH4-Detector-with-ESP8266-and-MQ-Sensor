#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ───────── CONFIG Wi-Fi ─────────
const char* ssid     = "Joma";
const char* password = "jm207021";

// ───────── CONFIG MQTT ─────────
const char* mqtt_server = "192.168.15.151";
const int   mqtt_port   = 1883;
const char* topicDHT    = "gas/dht";
const char* topicMQ     = "gas/mq";

WiFiClient espClient;
PubSubClient client(espClient);

// ───────── DHT11 ─────────
#define DHTPIN  D2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ───────── Variáveis vindas do Mega ─────────
char  lineBuffer[96];
int   lineIndex     = 0;
bool  haveMegaData  = false;

int   megaAdc       = 0;
float megaVrs       = 0.0;
float megaRs        = 0.0;
float megaRatio     = 0.0;
float megaPpm       = 0.0;
int   megaD0        = 0;

// Publicação periódica
unsigned long lastPublish    = 0;
const unsigned long interval = 5000;   // 5 s

// ───────── Helpers Wi-Fi / MQTT ─────────
void mqttCallback(char*, byte*, unsigned int) {
  // sem subscribe por enquanto
}

void setupWiFi() {
  delay(10);
  Serial.println();
  Serial.print("Conectando-se a ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Wi-Fi ok, IP: ");
  Serial.println(WiFi.localIP());
}

void connectToMQTT() {
  while (!client.connected()) {
    Serial.print("Tentando MQTT… ");
    if (client.connect("ESP8266_MQ5_BRIDGE")) {
      Serial.println("conectado");
    } else {
      Serial.print("falha (rc=");
      Serial.print(client.state());
      Serial.println("), tentando em 5s");
      delay(5000);
    }
  }
}

// ───────── Ler linha que vem do Mega ─────────
// Formato esperado:
//   ADC:123;VRS:1.234;RS:12345;RATIO:6.789;PPM:12.3;D0:0
void readFromMega() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();

    if (c == '\n' || c == '\r') {
      if (lineIndex > 0) {
        lineBuffer[lineIndex] = '\0';

        int   adc;
        float vrs, rs, ratio, ppm;
        int   d0;

        int matched = sscanf(lineBuffer,
                             "ADC:%d;VRS:%f;RS:%f;RATIO:%f;PPM:%f;D0:%d",
                             &adc, &vrs, &rs, &ratio, &ppm, &d0);

        if (matched == 6) {
          megaAdc   = adc;
          megaVrs   = vrs;
          megaRs    = rs;
          megaRatio = ratio;
          megaPpm   = ppm;
          megaD0    = d0;
          haveMegaData = true;

          Serial.print("Linha do Mega: ");
          Serial.println(lineBuffer);
        } else {
          Serial.print("Linha invalida do Mega: ");
          Serial.println(lineBuffer);
        }

        lineIndex = 0;
      }
    } else {
      if (lineIndex < (int)sizeof(lineBuffer) - 1) {
        lineBuffer[lineIndex++] = c;
      } else {
        lineIndex = 0;  // overflow -> reseta
      }
    }
  }
}

// ───────── SETUP ─────────
void setup() {
  // Serial hardware: RXD0/TXD0 (GPIO3/GPIO1)
  Serial.begin(9600);   // mesma baud da Serial3 do Mega
  delay(2000);

  dht.begin();
  setupWiFi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
  connectToMQTT();

  Serial.println("ESP8266 pronto (RXD0 <- Mega TX3, MQTT ativo).");
}

// ───────── LOOP ─────────
void loop() {
  if (!client.connected()) {
    connectToMQTT();
  }
  client.loop();

  // escuta o Mega o tempo todo
  readFromMega();

  unsigned long now = millis();
  if (now - lastPublish > interval) {
    lastPublish = now;

    // 1) Lê DHT11
    float hum  = dht.readHumidity();
    float temp = dht.readTemperature();

    if (!isnan(hum) && !isnan(temp)) {
      char payloadDHT[64];
      snprintf(payloadDHT, sizeof(payloadDHT),
               "{\"temp\":%.1f,\"hum\":%.1f}", temp, hum);
      client.publish(topicDHT, payloadDHT);
      Serial.print("Publicado DHT: ");
      Serial.println(payloadDHT);
    } else {
      Serial.println("Falha ao ler DHT11");
    }

    // 2) Publica dados do MQ-5 (se já recebeu algo do Mega)
    if (haveMegaData) {
      char payloadMQ[200];
      snprintf(payloadMQ, sizeof(payloadMQ),
               "{\"adc\":%d,"
               "\"v_rs\":%.3f,"
               "\"rs\":%.0f,"
               "\"ratio\":%.3f,"
               "\"ppm\":%.1f,"
               "\"d0\":%d,"
               "\"temp\":%.1f,"
               "\"hum\":%.1f}",
               megaAdc,
               megaVrs,
               megaRs,
               megaRatio,
               megaPpm,
               megaD0,
               temp,
               hum);

      client.publish(topicMQ, payloadMQ);
      Serial.print("Publicado MQ: ");
      Serial.println(payloadMQ);
    } else {
      Serial.println("Ainda nao recebeu dados do Mega.");
    }
  }
}
