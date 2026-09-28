#include <WiFi.h>
#include <WebServer.h>

// --- CONFIGURAÇÕES DE WI-FI ---
const char* ssid = "FamiliaOliveira";
const char* password = "Lucc181312";

// Cria o servidor web na porta padrão HTTP (80)
WebServer server(80);

const int TRIG_PIN = 25;   // mesmo pino do projeto final - ajuste se sua fiação for diferente
const int ECHO_PIN = 26;  // mesmo pino do projeto final - ajuste se sua fiação for diferente

const unsigned long INTERVALO_LEITURA = 2000;
unsigned long ultimaLeitura = 0;

float medirDistanciaCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duracao = pulseIn(ECHO_PIN, HIGH, 30000); // timeout de 30ms

  if (duracao == 0) {
    Serial.println("[SENSOR] AVISO: sem retorno do eco (fora de alcance, objeto");
    Serial.println("         muito perto/longe, ou erro de fiacao no TRIG/ECHO).");
    return -1.0;
  }

  float distanciaCm = (duracao * 0.0343) / 2.0;
  return distanciaCm;
}

// --- FUNÇÃO DA PÁGINA WEB ---
void handleRoot() {
  // Quando alguém acessar o site, faz uma medição nova
  float distancia = medirDistanciaCm();
  
  String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">";
  html += "<title>Teste EcoSmart</title>";
  html += "<style>body { font-family: Arial; text-align: center; margin-top: 50px; }</style>";
  html += "</head><body>";
  html += "<h1>Teste do Sensor HC-SR04 via Wi-Fi</h1>";
  
  if (distancia >= 0) {
    html += "<p style=\"font-size: 24px;\">Distância: <b>" + String(distancia) + " cm</b></p>";
  } else {
    html += "<p style=\"color: red;\">Aviso: sem retorno do eco (fora de alcance ou erro de fiação).</p>";
  }
  
  // Atualiza a página automaticamente a cada 2 segundos
  html += "<script>setTimeout(function(){location.reload()}, 2000);</script>"; 
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  Serial.println();
  Serial.println("===============================================");
  Serial.println(" TESTE DO SENSOR HC-SR04 (COM Servidor Wi-Fi)");
  Serial.println(" Aproxime/afaste um objeto do sensor para testar");
  Serial.println("===============================================");

  // --- INICIA A CONEXÃO WI-FI ---
  Serial.print("Conectando ao Wi-Fi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWi-Fi Conectado!");
  Serial.print("Abra o navegador e acesse o IP: ");
  Serial.println(WiFi.localIP()); 

  // Configura a rota principal do servidor ("/") para rodar a função handleRoot
  server.on("/", handleRoot);
  server.begin();
}

void loop() {
  // 1. Mantém o servidor web funcionando para responder ao navegador
  server.handleClient();

  // 2. Mantém o código original do Monitor Serial rodando independentemente
  unsigned long agora = millis();

  if (agora - ultimaLeitura >= INTERVALO_LEITURA) {
    ultimaLeitura = agora;

    float distancia = medirDistanciaCm();

    if (distancia >= 0) {
      Serial.print("[MEDICAO] Distancia: ");
      Serial.print(distancia);
      Serial.println(" cm");
    }
  }
}