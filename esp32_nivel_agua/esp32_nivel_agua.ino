/*
  EcoSmart Monitor - Firmware ESP32 + HC-SR04
  ---------------------------------------------
  Mede a distância até a superfície da água e classifica o nível como
  NORMAL, MEDIA ou ALTA, enviando os dados via HTTP POST para o backend
  FastAPI.

  IMPORTANTE - AJUSTE ANTES DE USAR:
  1) Preencha SSID e SENHA do WiFi
  2) Preencha o IP/porta do seu backend (SERVER_URL)
  3) Confirme os pinos do HC-SR04 (TRIG_PIN / ECHO_PIN)
  4) Confirme a LÓGICA dos limiares (ver comentário abaixo) de acordo com
     a posição física do sensor no seu reservatório/rio/caixa d'água.

  Bibliotecas necessárias (instalar pela Arduino IDE > Gerenciador de Bibliotecas):
  - WiFi.h        (já vem com o core do ESP32)
  - HTTPClient.h  (já vem com o core do ESP32)
  - ArduinoJson   (procurar por "ArduinoJson" de Benoit Blanchon)
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ---------------------- CONFIGURAÇÕES DE REDE ----------------------
const char* WIFI_SSID = "NOME_DA_SUA_REDE";
const char* WIFI_PASSWORD = "SENHA_DA_SUA_REDE";

// Endereço do seu backend FastAPI (IP do notebook/servidor + porta 8000)
const char* SERVER_URL = "http://192.168.0.100:8000/leituras";

// Identificador deste dispositivo (deve bater com o que você quiser ver no banco)
const char* DEVICE_ID = "esp32-caixa-agua-01";

// ---------------------- CONFIGURAÇÕES DO SENSOR ----------------------
const int TRIG_PIN = 5;   // ajuste conforme sua fiação
const int ECHO_PIN = 18;  // ajuste conforme sua fiação

// ---------------------- LIMIARES DE CLASSIFICAÇÃO (cm) ----------------------
// ATENÇÃO: estes valores representam a DISTÂNCIA medida pelo sensor até a água,
// não a altura da água em si. Ajuste a lógica de comparação em classificarNivel()
// se a montagem física do seu sensor for diferente (ex: sensor apontando de baixo
// pra cima dentro de um poço).
const float LIMIAR_NORMAL = 5.0;   // distância <= 5 cm  -> nível ALTA (água perto do sensor)
const float LIMIAR_MEDIA  = 10.0;  // distância <= 10 cm -> nível MEDIA
const float LIMIAR_ALTA   = 15.0;  // distância <= 15 cm -> nível NORMAL (água mais longe)

// Intervalo entre medições (ms)
const unsigned long INTERVALO_ENVIO = 10000; // 10 segundos

unsigned long ultimoEnvio = 0;

// ---------------------- FUNÇÕES ----------------------

void conectarWiFi() {
  Serial.print("[WIFI] Conectando na rede '");
  Serial.print(WIFI_SSID);
  Serial.print("'");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }

  Serial.println();
  Serial.println("[WIFI] Conectado com sucesso!");
  Serial.print("[WIFI] IP do ESP32: ");
  Serial.println(WiFi.localIP());
  Serial.print("[WIFI] Sinal (RSSI): ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
}

// Mede a distância em cm usando o HC-SR04
float medirDistanciaCm() {
  // Garante que o TRIG está em nível baixo antes do pulso
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  // Envia o pulso de 10 microssegundos que dispara a medição
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Lê o tempo (em microssegundos) que o pino ECHO ficou em HIGH
  // timeout de 30ms evita travar caso não haja retorno de eco
  long duracao = pulseIn(ECHO_PIN, HIGH, 30000);

  Serial.println("---------------------------------------------");
  Serial.print("[SENSOR] Duracao do pulso (echo): ");
  Serial.print(duracao);
  Serial.println(" us");

  if (duracao == 0) {
    Serial.println("[SENSOR] AVISO: sem retorno do eco (fora de alcance, objeto");
    Serial.println("         muito perto/longe, ou erro de fiacao no TRIG/ECHO).");
    return -1.0;
  }

  // Velocidade do som ~0.0343 cm/us; divide por 2 porque o pulso vai e volta
  float distanciaCm = (duracao * 0.0343) / 2.0;

  Serial.print("[SENSOR] Distancia calculada: ");
  Serial.print(distanciaCm);
  Serial.println(" cm");

  return distanciaCm;
}

// Classifica o nível de água com base na distância medida
String classificarNivel(float distanciaCm) {
  String status;

  if (distanciaCm <= LIMIAR_NORMAL) {
    status = "alta";
  } else if (distanciaCm <= LIMIAR_MEDIA) {
    status = "media";
  } else if (distanciaCm <= LIMIAR_ALTA) {
    status = "normal";
  } else {
    status = "abaixo_do_normal"; // distância maior que todos os limiares
  }

  Serial.print("[CLASSIFICACAO] Nivel de agua: ");
  Serial.print(status);
  Serial.print("  (limiares: alta<=");
  Serial.print(LIMIAR_NORMAL);
  Serial.print("cm | media<=");
  Serial.print(LIMIAR_MEDIA);
  Serial.print("cm | normal<=");
  Serial.print(LIMIAR_ALTA);
  Serial.println("cm)");

  return status;
}

// Envia os dados medidos para o backend FastAPI
void enviarLeitura(float distanciaCm, const String& statusNivel) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] Desconectado. Tentando reconectar...");
    conectarWiFi();
  }

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");

  // Monta o JSON no mesmo formato esperado pelo endpoint /leituras
  StaticJsonDocument<256> doc;
  doc["device_id"] = DEVICE_ID;
  doc["distancia_cm"] = distanciaCm;
  doc["nivel_agua_cm"] = distanciaCm; // ajuste aqui se quiser enviar altura já convertida

  String corpoJson;
  serializeJson(doc, corpoJson);

  Serial.print("[HTTP] Destino: ");
  Serial.println(SERVER_URL);
  Serial.print("[HTTP] Corpo enviado: ");
  Serial.println(corpoJson);

  unsigned long inicio = millis();
  int codigoResposta = http.POST(corpoJson);
  unsigned long duracaoRequisicao = millis() - inicio;

  if (codigoResposta > 0) {
    Serial.print("[HTTP] Sucesso! Codigo: ");
    Serial.print(codigoResposta);
    Serial.print(" | Tempo: ");
    Serial.print(duracaoRequisicao);
    Serial.println(" ms");
    Serial.print("[HTTP] Resposta do servidor: ");
    Serial.println(http.getString());
  } else {
    Serial.print("[HTTP] ERRO ao enviar POST: ");
    Serial.println(http.errorToString(codigoResposta));
    Serial.println("[HTTP] Verifique: IP do servidor, se a API esta rodando,");
    Serial.println("       e se o ESP32 esta na mesma rede.");
  }

  http.end();
  Serial.println("---------------------------------------------");
}

// ---------------------- SETUP E LOOP ----------------------

void setup() {
  Serial.begin(115200);
  delay(1000); // tempo pro monitor serial abrir e não perder as primeiras linhas

  Serial.println();
  Serial.println("===============================================");
  Serial.println(" EcoSmart Monitor - Iniciando ESP32 + HC-SR04");
  Serial.println("===============================================");

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  Serial.println("[SETUP] Pinos do sensor configurados.");

  conectarWiFi();

  Serial.println("[SETUP] Pronto! Iniciando ciclo de medicoes...");
}

void loop() {
  unsigned long agora = millis();

  if (agora - ultimoEnvio >= INTERVALO_ENVIO) {
    ultimoEnvio = agora;

    Serial.println();
    Serial.print("[CICLO] Nova medicao em t=");
    Serial.print(agora / 1000);
    Serial.println("s");

    float distancia = medirDistanciaCm();

    if (distancia >= 0) {
      String nivel = classificarNivel(distancia);
      enviarLeitura(distancia, nivel);
    } else {
      Serial.println("[CICLO] Medicao invalida, pulando envio deste ciclo.");
      Serial.println("---------------------------------------------");
    }
  }
}
