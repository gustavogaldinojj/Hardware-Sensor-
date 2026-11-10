/*
  Teste do HC-SR04 lendo o ECHO de forma ANALOGICA
  ------------------------------------------------
  Serve para o divisor com 2 resistores iguais (200 + 200 ohms), que entrega
  so ~2,5 V - no limite do que o ESP32 reconhece como nivel alto no digital.

  Aqui o ESP32 mede a TENSAO do Echo, entao:
   - se o sensor estiver vivo, aparece a tensao maxima (~2 a 2,5 V) e a distancia
   - se aparecer ~0 V sempre, o sensor nao esta respondendo (provavelmente queimado)

  FIACAO PARA ESTE TESTE:
   - Azul (Trig)    -> D26
   - Amarelo (Echo) -> mude do D25 para o D33 (um pino acima)
     O D33 e um pino ADC1, que continua funcionando com o WiFi ligado.
*/

#include <WiFi.h>

const int TRIG_PIN = 26;
const int ECHO_PIN = 33;

// Limiar em leitura bruta (0-4095). ~1000 = ~0,8 V
const int LIMIAR = 1000;

void setup() {
  Serial.begin(115200);
  delay(1000);
  WiFi.mode(WIFI_OFF);

  pinMode(TRIG_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);

  analogReadResolution(12);
  analogSetPinAttenuation(ECHO_PIN, ADC_11db); 

  Serial.println();
  Serial.println("=== Teste analogico HC-SR04 ===");
}

void loop() {
  // Disparo
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(5);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Amostra o Echo por ate 30 ms
  int maximo = 0;
  unsigned long inicio = 0, fim = 0;
  unsigned long t0 = micros();

  while (micros() - t0 < 30000) {
    int v = analogRead(ECHO_PIN);
    if (v > maximo) maximo = v;
    if (v > LIMIAR && inicio == 0) inicio = micros();
    if (v < LIMIAR && inicio != 0) { fim = micros(); break; }
  }

  float voltsMax = maximo * 3.3 / 4095.0;

  Serial.print("Echo max: ");
  Serial.print(voltsMax, 2);
  Serial.print(" V (bruto ");
  Serial.print(maximo);
  Serial.print(")");

  if (inicio != 0 && fim != 0) {
    unsigned long dur = fim - inicio;
    Serial.print(" | pulso=");
    Serial.print(dur);
    Serial.print(" us -> ");
    Serial.print(dur * 0.0343 / 2.0);
    Serial.println(" cm");
  } else if (inicio != 0) {
    Serial.println(" | sinal ficou alto (sem fim do pulso)");
  } else {
    Serial.println(" | sem pulso");
  }

  delay(500);
}
