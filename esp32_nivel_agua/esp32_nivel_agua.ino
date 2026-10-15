/*
  EcoSmart Monitor - Firmware ESP32 + HC-SR04 (CORRIGIDO)
  ---------------------------------------------------------
  Mede a distância até a superfície da água e envia a leitura ao backend
  FastAPI, no formato exato que o endpoint /api/v1/sensores/nivel-agua
  espera.

  PRÉ-REQUISITO OBRIGATÓRIO (fazer ANTES de gravar este firmware):
  Cadastre o sensor uma única vez, chamando o endpoint administrativo
  (troque a URL, o token admin e os valores conforme seu projeto):

    curl -X PUT https://SEU-BACKEND/api/v1/sensores/config \
         -H "X-Admin-Token: SEU_API_ADMIN_TOKEN" \
         -H "Content-Type: application/json" \
         -d '{
               "nome": "Caixa dagua - Casa",
               "latitude": -23.55,
               "longitude": -46.63,
               "altura_instalacao_cm": 100,
               "limiar_alerta_cm": 80
             }'

  A resposta traz um campo "device_token" — copie o valor e cole em
  DEVICE_TOKEN abaixo. Sem isso, toda leitura enviada recebe 403.

  AJUSTE ANTES DE USAR:
  1) SSID e SENHA do WiFi
  2) SERVER_HOST (IP local para testes em rede, ou domínio do Render)
  3) USE_HTTPS (true se for direto pro Render, false se for backend local http)
  4) DEVICE_TOKEN (copiado do passo acima)
  5) Pinos do HC-SR04 (TRIG_PIN / ECHO_PIN), se sua fiação for diferente

  FIAÇÃO ATUAL:
  - Vermelho -> VIN (5V)      -> VCC do sensor
  - Preto    -> GND           -> GND do sensor
  - Azul     -> D26 (GPIO 26) -> TRIG
  - Amarelo  -> D25 (GPIO 25) -> ECHO (via divisor de tensão 1k / 2k)
  Se a duração do eco der sempre 0 us, inverta TRIG_PIN e ECHO_PIN.

  Bibliotecas necessárias (Arduino IDE > Gerenciador de Bibliotecas):
  - WiFi.h / WiFiClientSecure.h  (já vêm com o core do ESP32)
  - HTTPClient.h                 (já vem com o core do ESP32)
  - ArduinoJson                  (procurar "ArduinoJson" de Benoit Blanchon)
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ---------------------- CONFIGURAÇÕES DE REDE ----------------------
const char* WIFI_SSID = "Oliveira _Gustavo";
const char* WIFI_PASSWORD = "luccic181312";

// Para testar na mesma rede local com o backend rodando via Docker Compose:
//   SERVER_HOST = "192.168.0.100:8000"   (IP do seu computador na rede local)
//   USE_HTTPS   = false
// Para enviar direto pro backend hospedado no Render:
//   SERVER_HOST = "ecosmart-backend-apcv.onrender.com"
//   USE_HTTPS   = true
const char* SERVER_HOST = "192.168.0.101:8000";
const bool USE_HTTPS = false;

// Caminho FIXO e CORRETO do endpoint real (não mude isso)
const char* ENDPOINT_PATH = "/api/v1/sensores/nivel-agua";

// Token do dispositivo, obtido no cadastro via PUT /api/v1/sensores/config
// (ver instruções no topo deste arquivo). Sem isso, o backend responde 403.
const char* DEVICE_TOKEN = "COLE_AQUI_O_DEVICE_TOKEN_RECEBIDO_NO_CADASTRO";

// ---------------------- CONFIGURAÇÕES DO SENSOR ----------------------
const int TRIG_PIN = 26;  // fio azul  (D26)
const int ECHO_PIN = 25;  // fio amarelo (D25)

// Quantidade de leituras por ciclo (usa a mediana para filtrar ruído)
const int NUM_LEITURAS = 5;

// ---------------------- CLASSIFICAÇÃO LOCAL (SOMENTE PARA DEBUG NO SERIAL) ----------------------
// IMPORTANTE: estes limiares NÃO são enviados ao backend e NÃO influenciam
// a classificação de risco real do sistema. A classificação oficial é
// feita pelo backend, usando "altura_instalacao_cm" e "limiar_alerta_cm"
// configurados via API (mais fácil de ajustar sem regravar o firmware).
// Isto aqui serve só para você acompanhar no Monitor Serial durante testes.
const float LIMIAR_DEBUG_ALTA   = 5.0;
const float LIMIAR_DEBUG_MEDIA  = 10.0;
const float LIMIAR_DEBUG_NORMAL = 15.0;

// Intervalo entre medições (ms)
const unsigned long INTERVALO_ENVIO = 10000; // 10 segundos

// Tempo máximo tentando conectar no WiFi (ms)
const unsigned long WIFI_TIMEOUT = 15000; // 15 segundos

unsigned long ultimoEnvio = 0;

// ---------------------- FUNÇÕES ----------------------

// Tenta conectar no WiFi por até WIFI_TIMEOUT ms.
// Retorna true se conectou, false se não (o firmware continua medindo).
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

// Faz UMA medição de distância em cm usando o HC-SR04.
// Retorna -1.0 se não houver eco.
float medirDistanciaCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duracao = pulseIn(ECHO_PIN, HIGH, 30000);

  Serial.print("[SENSOR] Duracao do pulso (echo): ");
  Serial.print(duracao);
  Serial.print(" us");

  if (duracao == 0) {
    Serial.println("  -> sem eco");
    return -1.0;
  }

  float distanciaCm = (duracao * 0.0343) / 2.0;

  Serial.print("  -> ");
  Serial.print(distanciaCm);
  Serial.println(" cm");

  return distanciaCm;
}

// Faz várias medições e retorna a mediana das válidas (filtra leituras ruins).
// Retorna -1.0 se nenhuma leitura for válida.
float medirMediana() {
  Serial.println("---------------------------------------------");

  float valores[NUM_LEITURAS];
  int n = 0;

  for (int i = 0; i < NUM_LEITURAS; i++) {
    float d = medirDistanciaCm();
    if (d > 0) {
      valores[n++] = d;
    }
    delay(60); // espera o eco anterior sumir antes de disparar de novo
  }

  if (n == 0) {
    Serial.println("[SENSOR] AVISO: nenhum eco recebido (fora de alcance, objeto");
    Serial.println("         muito perto/longe, ou TRIG/ECHO trocados/mal conectados).");
    return -1.0;
  }

  // Ordena (bubble sort simples, poucos elementos)
  for (int i = 0; i < n - 1; i++) {
    for (int j = i + 1; j < n; j++) {
      if (valores[j] < valores[i]) {
        float tmp = valores[i];
        valores[i] = valores[j];
        valores[j] = tmp;
      }
    }
  }

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

// Apenas para acompanhar no Serial Monitor durante os testes - NÃO é enviado ao backend
void logClassificacaoDebug(float distanciaCm) {
  String status;
  if (distanciaCm <= LIMIAR_DEBUG_ALTA) status = "alta (debug local)";
  else if (distanciaCm <= LIMIAR_DEBUG_MEDIA) status = "media (debug local)";
  else if (distanciaCm <= LIMIAR_DEBUG_NORMAL) status = "normal (debug local)";
  else status = "abaixo_do_normal (debug local)";

  Serial.print("[DEBUG LOCAL] Classificacao aproximada: ");
  Serial.println(status);
  Serial.println("[DEBUG LOCAL] A classificacao OFICIAL e feita pelo backend,");
  Serial.println("              usando a altura de instalacao e o limiar de alerta");
  Serial.println("              configurados via API (PUT /api/v1/sensores/config).");
}

// Envia a leitura para o backend, no formato exato esperado por SensorLeituraIn
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
    // Simplificação pragmática para um dispositivo de telemetria: não valida
    // a cadeia de certificado do servidor (o Render usa HTTPS com CA pública,
    // mas validar certificados no ESP32 exige fixar/atualizar o certificado
    // raiz manualmente). A autenticação de quem pode enviar dados continua
    // garantida pelo header X-Device-Token abaixo.
    clienteSeguro.setInsecure();
    iniciou = http.begin(clienteSeguro, url);
  } else {
    iniciou = http.begin(url);
  }

  if (!iniciou) {
    Serial.println("[HTTP] ERRO: falha ao iniciar a conexao HTTP(S).");
    return;
  }

  http.setTimeout(15000); // Render pode demorar para "acordar"
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Token", DEVICE_TOKEN);

  // Formato EXATO esperado pelo schema SensorLeituraIn do backend:
  // só "distancia_cm" (obrigatorio) e "bateria_v" (opcional, omitido aqui).
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
      Serial.println("       Confira o token com GET /api/v1/sensores/config.");
    }
  } else {
    Serial.print("[HTTP] ERRO ao enviar POST: ");
    Serial.println(http.errorToString(codigoResposta));
    Serial.println("[HTTP] Verifique: host/porta do servidor, USE_HTTPS, e se o");
    Serial.println("       ESP32 tem acesso de rede ate o backend.");
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
  Serial.println(" EcoSmart Monitor - Iniciando ESP32 + HC-SR04");
  Serial.println("===============================================");

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  Serial.print("[SETUP] Pinos do sensor configurados. TRIG=");
  Serial.print(TRIG_PIN);
  Serial.print(" ECHO=");
  Serial.println(ECHO_PIN);

  // Busca de redes (diagnostico)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  Serial.println("[WIFI] Procurando redes...");
  int n = WiFi.scanNetworks();
  Serial.print("[WIFI] Redes encontradas: ");
  Serial.println(n);
  for (int i = 0; i < n; i++) {
    Serial.print("  ");
    Serial.print(WiFi.SSID(i));
    Serial.print(" (");
    Serial.print(WiFi.RSSI(i));
    Serial.println(" dBm)");
  }

  conectarWiFi();

  Serial.println("[SETUP] Pronto! Iniciando ciclo de medicoes...");

  // Forca a primeira medicao imediatamente
  ultimoEnvio = millis() - INTERVALO_ENVIO;
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
