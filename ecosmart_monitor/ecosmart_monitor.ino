/*
  EcoSmart Monitor - Firmware ESP32 + HC-SR04 (LEITURA ANALOGICA DO ECHO)
  -----------------------------------------------------------------------
  Mede a distância até a superfície da água e envia a leitura ao backend
  FastAPI (endpoint /api/v1/sensores/nivel-agua).

  POR QUE LEITURA ANALOGICA?
  O divisor de tensão usa dois resistores iguais (200 + 200 ohms), que
  entregam só ~2,5 V ao ESP32 - no limite do nível alto digital. Lendo a
  TENSÃO do Echo pelo ADC, o pulso é detectado com folga.

  FIAÇÃO:
  - Vermelho -> VIN (5V)       -> Vcc do sensor
  - Preto    -> GND            -> Gnd do sensor
  - Azul     -> D26 (GPIO 26)  -> Trig
  - Amarelo  -> D33 (GPIO 33)  -> ponto do meio do divisor (Echo/2)
  Divisor na protoboard: Echo -> 200 ohms -> ponto do meio -> 200 ohms -> Gnd
  O D33 é um pino ADC1, que continua funcionando com o WiFi ligado.

  PRÉ-REQUISITO: cadastrar o sensor via PUT /api/v1/sensores/config e colar
  o "device_token" em DEVICE_TOKEN. Sem isso, o backend responde 403.

    curl -X PUT http://SEU-BACKEND/api/v1/sensores/config \
         -H "X-Admin-Token: SEU_API_ADMIN_TOKEN" \
         -H "Content-Type: application/json" \
         -d '{"nome":"Caixa dagua - Casa","latitude":-23.55,"longitude":-46.63,
              "altura_instalacao_cm":100,"limiar_alerta_cm":80}'

  Bibliotecas: WiFi, WiFiClientSecure, HTTPClient (core ESP32) e
  ArduinoJson (Benoit Blanchon).
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ---------------------- CONFIGURAÇÕES DE REDE ----------------------
const char* WIFI_SSID = "Oliveira _Gustavo";
const char* WIFI_PASSWORD = "SENHA_DA_SUA_REDE";

// Backend local (mesma rede):  "192.168.0.101:8000" e USE_HTTPS = false
// Backend no Render:           "ecosmart-backend-apcv.onrender.com" e USE_HTTPS = true
const char* SERVER_HOST = "192.168.0.101:8000";
const bool USE_HTTPS = false;

const char* ENDPOINT_PATH = "/api/v1/sensores/nivel-agua";

const char* DEVICE_TOKEN = "COLE_AQUI_O_DEVICE_TOKEN_RECEBIDO_NO_CADASTRO";

// ---------------------- CONFIGURAÇÕES DO SENSOR ----------------------
const int TRIG_PIN = 26;  // fio azul
const int ECHO_PIN = 33;  // fio amarelo (ADC1)

// Limiar do ADC (0-4095) para considerar o Echo "alto".
// ~1000 = ~0,8 V. Com o divisor, o pulso chega a ~2,4 V (~3000).
const int LIMIAR_ADC = 1000;

// Tempo máximo esperando o pulso (us). 30 ms ~ 5 m.
const unsigned long TIMEOUT_US = 30000;

// Leituras por ciclo (usa a mediana para filtrar ruído)
const int NUM_LEITURAS = 5;

// ---------------------- CLASSIFICAÇÃO LOCAL (SÓ DEBUG) ----------------------
// Não é enviada ao backend; a classificação oficial é feita pelo backend.
const float LIMIAR_DEBUG_ALTA   = 5.0;
const float LIMIAR_DEBUG_MEDIA  = 10.0;
const float LIMIAR_DEBUG_NORMAL = 15.0;

const unsigned long INTERVALO_ENVIO = 10000; // 10 s entre ciclos
const unsigned long WIFI_TIMEOUT = 15000;    // 15 s tentando conectar

unsigned long ultimoEnvio = 0;

// ---------------------- WIFI ----------------------

bool conectarWiFi() {
  Serial.print("[WIFI] Conectando na rede '");
  Serial.print(WIFI_SSID);
  Serial.print("'");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] Falhou ao conectar. Seguindo sem rede (so medicao local).");
    Serial.println("[WIFI] Verifique SSID/senha e se a rede e 2.4 GHz.");
    return false;
  }

  Serial.println("[WIFI] Conectado com sucesso!");
  Serial.print("[WIFI] IP do ESP32: ");
  Serial.println(WiFi.localIP());
  Serial.print("[WIFI] Sinal (RSSI): ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  return true;
}

// ---------------------- SENSOR ----------------------

// Faz UMA medição lendo o Echo pelo ADC. Retorna -1.0 se não houver pulso.
float medirDistanciaCm() {
  // Disparo
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(5);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // 1) Espera o Echo subir
  unsigned long t0 = micros();
  while (analogRead(ECHO_PIN) < LIMIAR_ADC) {
    if (micros() - t0 > TIMEOUT_US) {
      Serial.println("[SENSOR] sem pulso (Echo nao subiu)");
      return -1.0;
    }
  }
  unsigned long inicio = micros();

  // 2) Mede quanto tempo fica alto
  while (analogRead(ECHO_PIN) >= LIMIAR_ADC) {
    if (micros() - inicio > TIMEOUT_US) {
      Serial.println("[SENSOR] Echo ficou alto (sem fim do pulso)");
      return -1.0;
    }
  }
  unsigned long duracao = micros() - inicio;

  float distanciaCm = duracao * 0.0343 / 2.0;

  Serial.print("[SENSOR] pulso=");
  Serial.print(duracao);
  Serial.print(" us -> ");
  Serial.print(distanciaCm);
  Serial.println(" cm");

  return distanciaCm;
}

// Faz várias medições e retorna a mediana das válidas. -1.0 se nenhuma.
float medirMediana() {
  Serial.println("---------------------------------------------");

  float valores[NUM_LEITURAS];
  int n = 0;

  for (int i = 0; i < NUM_LEITURAS; i++) {
    float d = medirDistanciaCm();
    if (d > 0) valores[n++] = d;
    delay(60); // espera o eco anterior sumir
  }

  if (n == 0) {
    Serial.println("[SENSOR] AVISO: nenhuma leitura valida.");
    return -1.0;
  }

  for (int i = 0; i < n - 1; i++)
    for (int j = i + 1; j < n; j++)
      if (valores[j] < valores[i]) { float t = valores[i]; valores[i] = valores[j]; valores[j] = t; }

  float mediana = valores[n / 2];

  Serial.print("[SENSOR] Leituras validas: ");
  Serial.print(n);
  Serial.print("/");
  Serial.print(NUM_LEITURAS);
  Serial.print(" | Mediana: ");
  Serial.print(mediana);
  Serial.println(" cm");

  return mediana;
}

void logClassificacaoDebug(float distanciaCm) {
  String status;
  if (distanciaCm <= LIMIAR_DEBUG_ALTA) status = "alta (debug local)";
  else if (distanciaCm <= LIMIAR_DEBUG_MEDIA) status = "media (debug local)";
  else if (distanciaCm <= LIMIAR_DEBUG_NORMAL) status = "normal (debug local)";
  else status = "abaixo_do_normal (debug local)";

  Serial.print("[DEBUG LOCAL] Classificacao aproximada: ");
  Serial.println(status);
}

// ---------------------- ENVIO AO BACKEND ----------------------

void enviarLeitura(float distanciaCm) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] Desconectado. Tentando reconectar...");
    if (!conectarWiFi()) {
      Serial.println("[HTTP] Sem WiFi, leitura nao enviada neste ciclo.");
      Serial.println("---------------------------------------------");
      return;
    }
  }

  String url = String(USE_HTTPS ? "https://" : "http://") + SERVER_HOST + ENDPOINT_PATH;

  HTTPClient http;
  WiFiClientSecure clienteSeguro;

  bool iniciou;
  if (USE_HTTPS) {
    clienteSeguro.setInsecure(); // não valida certificado; autenticação via X-Device-Token
    iniciou = http.begin(clienteSeguro, url);
  } else {
    iniciou = http.begin(url);
  }

  if (!iniciou) {
    Serial.println("[HTTP] ERRO: falha ao iniciar a conexao HTTP(S).");
    return;
  }

  http.setTimeout(15000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Token", DEVICE_TOKEN);

  StaticJsonDocument<128> doc;
  doc["distancia_cm"] = distanciaCm;

  String corpoJson;
  serializeJson(doc, corpoJson);

  Serial.print("[HTTP] Destino: ");
  Serial.println(url);
  Serial.print("[HTTP] Corpo enviado: ");
  Serial.println(corpoJson);

  unsigned long inicio = millis();
  int codigoResposta = http.POST(corpoJson);
  unsigned long duracaoRequisicao = millis() - inicio;

  if (codigoResposta > 0) {
    Serial.print("[HTTP] Codigo: ");
    Serial.print(codigoResposta);
    Serial.print(" | Tempo: ");
    Serial.print(duracaoRequisicao);
    Serial.println(" ms");
    Serial.print("[HTTP] Resposta do servidor: ");
    Serial.println(http.getString());

    if (codigoResposta == 403) {
      Serial.println("[HTTP] 403 = DEVICE_TOKEN incorreto ou sensor nao cadastrado.");
    }
  } else {
    Serial.print("[HTTP] ERRO ao enviar POST: ");
    Serial.println(http.errorToString(codigoResposta));
    Serial.println("[HTTP] Verifique host/porta, USE_HTTPS e se o backend esta rodando.");
  }

  http.end();
  Serial.println("---------------------------------------------");
}

// ---------------------- SETUP E LOOP ----------------------

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("===============================================");
  Serial.println(" EcoSmart Monitor - ESP32 + HC-SR04 (ADC)");
  Serial.println("===============================================");

  pinMode(TRIG_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);

  analogReadResolution(12);
  analogSetPinAttenuation(ECHO_PIN, ADC_11db); // faixa até ~3,3 V

  Serial.print("[SETUP] TRIG=");
  Serial.print(TRIG_PIN);
  Serial.print(" ECHO(ADC)=");
  Serial.println(ECHO_PIN);

  conectarWiFi();

  Serial.println("[SETUP] Pronto! Iniciando ciclo de medicoes...");
  ultimoEnvio = millis() - INTERVALO_ENVIO; // primeira medição imediata
}

void loop() {
  unsigned long agora = millis();

  if (agora - ultimoEnvio >= INTERVALO_ENVIO) {
    ultimoEnvio = agora;

    Serial.println();
    Serial.print("[CICLO] Nova medicao em t=");
    Serial.print(agora / 1000);
    Serial.println("s");

    float distancia = medirMediana();

    if (distancia >= 0) {
      logClassificacaoDebug(distancia);
      enviarLeitura(distancia);
    } else {
      Serial.println("[CICLO] Medicao invalida, pulando envio deste ciclo.");
      Serial.println("---------------------------------------------");
    }
  }
}
