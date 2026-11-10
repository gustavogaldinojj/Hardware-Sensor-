/*
  Teste de diagnostico do HC-SR04 no ESP32
  ----------------------------------------
  Sem WiFi, sem backend. So testa o sensor.
  Testa as DUAS combinacoes de pinos (26/25 e 25/26) e mostra:
   - o estado do pino ECHO em repouso
   - se o ECHO chega a subir depois do disparo
   - a duracao do pulso e a distancia

  Abra o Monitor Serial em 115200 e aponte o sensor para uma
  parede lisa a 20-30 cm.
*/

#include <WiFi.h>

const int PINO_A = 26;  // fio azul
const int PINO_B = 33;  // fio amarelo

void testar(int trig, int echo) {
  pinMode(trig, OUTPUT);
  pinMode(echo, INPUT);
  digitalWrite(trig, LOW);
  delay(50);

  Serial.print("TRIG=");
  Serial.print(trig);
  Serial.print(" ECHO=");
  Serial.print(echo);

  // Estado do ECHO em repouso (deve ser 0)
  int repouso = digitalRead(echo);
  Serial.print(" | repouso=");
  Serial.print(repouso);

  // Dispara
  digitalWrite(trig, LOW);
  delayMicroseconds(5);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  // Observa o ECHO por ate 40 ms: chegou a subir?
  bool subiu = false;
  unsigned long t0 = micros();
  unsigned long inicioAlto = 0, fimAlto = 0;
  while (micros() - t0 < 40000) {
    int v = digitalRead(echo);
    if (v == HIGH && !subiu) { subiu = true; inicioAlto = micros(); }
    if (v == LOW && subiu && fimAlto == 0) { fimAlto = micros(); break; }
  }

  Serial.print(" | echo subiu=");
  Serial.print(subiu ? "SIM" : "NAO");

  if (subiu && fimAlto > 0) {
    unsigned long dur = fimAlto - inicioAlto;
    Serial.print(" | pulso=");
    Serial.print(dur);
    Serial.print(" us -> ");
    Serial.print(dur * 0.0343 / 2.0);
    Serial.print(" cm");
  } else if (subiu) {
    Serial.print(" | ficou ALTO o tempo todo");
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  WiFi.mode(WIFI_OFF);  // garante que o WiFi nao interfere
  Serial.println();
  Serial.println("=== Teste HC-SR04 ===");
}

void loop() {
  testar(PINO_A, PINO_B);  // TRIG=26, ECHO=25
  delay(100);
  testar(PINO_B, PINO_A);  // TRIG=25, ECHO=26
  Serial.println("---");
  delay(1000);
}
