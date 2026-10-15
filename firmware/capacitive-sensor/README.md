# Firmware — sensor capacitivo

Sketch para **Arduino NANO** (o compatible) que lee un umbral metálico capacitivo y envía el valor por **USB Serial** a RUMOR.

## Requisitos

- Arduino IDE / CLI
- Librería [CapacitiveSensor](https://www.arduino.cc/reference/en/libraries/capacitivesensor/) (Paul Badger)

## Cableado

```text
Arduino pin 4 ── resistencia (1–10 MΩ) ──┬── pin 2
                                         └── metal (placa / alcantarilla)
GND ── común con la instalación según diseño
```

## Flashear

1. Abrí `capacitive-sensor.ino` en el Arduino IDE.
2. Placa: Arduino Nano, puerto USB correcto.
3. Upload.

## Protocolo

| Parámetro | Valor                          |
| --------- | ------------------------------ |
| Baud      | **9600**                       |
| Formato   | una línea con un entero (`println`) |
| Periodo   | ~50 ms + tiempo de lectura     |

La app RUMOR espera este stream en el puerto serial configurado (`config/serial.json` / UI).
