/*
  RUMOR — sensor capacitivo (Arduino NANO)

  Cableado:
    pin 4 -- resistencia (p. ej. 1–10 MΩ) -- pin 2
                                           -- metal (alcantarilla / umbral)

  Salida: un entero por línea a 9600 baud (USB Serial) para la app RUMOR.
  Librería: CapacitiveSensor (Paul Badger) vía Library Manager.
*/

#include <CapacitiveSensor.h>

// pin4 -- Resistencia -- pin 2
//                     -- metal
CapacitiveSensor sCapacitivoPin4Y2 = CapacitiveSensor(4, 2);

void setup() {
  Serial.begin(9600);
}

void loop() {
  long lectura = sCapacitivoPin4Y2.capacitiveSensor(40);
  Serial.println(lectura);
  delay(50);
}
