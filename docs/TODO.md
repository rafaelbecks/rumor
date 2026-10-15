# RUMOR — TODO

## Calibración remota vía WebSockets

Definir e implementar el sistema de calibración en tiempo real (hoy referenciado como app web en `rumor.luminode.studio:5000`):

- [ ] Protocolo WebSocket / Socket.IO entre la Pi (o el host JUCE) y la app de calibración
- [ ] Mensajes de estado: lectura cruda del sensor, min/max de ventana, destinos activos, motor (sinte / paulxstretch)
- [ ] Comandos remotos: ajustar `inputLow` / `inputHigh`, destinos serial, drone, waveform/scale, start/stop engine
- [ ] Persistencia de presets de calibración (sin guardar puerto serial)
- [ ] Autenticación / token de sesión para instalaciones en red pública
- [ ] UI web de curva de mapeo (entrada → destino) con preview en vivo

## Presencia — sensor PIR HC-SR501

Integrar un **sensor infrarrojo de movimiento PIR HC-SR501** para detectar presencia en la instalación:

- [ ] Cableado tipico: VCC 5 V, GND, OUT digital → GPIO Raspberry Pi (o Arduino auxiliar)
- [ ] Firmware / daemon: debounce (~2–3 s de retrigger), histéresis de “persona presente / ausente”
- [ ] Puente a RUMOR: serie, GPIO→Socket, o mensaje WebSocket (`presence: on|off`)
- [ ] Comportamiento de instalación: despertar audio / proyectores al detectar presencia; idle / silence tras timeout
- [ ] Documentar zona de detección (~3–7 m, ángulo ~110°) y montaje respecto a la alcantarilla / recipiente

## Builds

- [ ] Publicar artefactos CI (macOS `.app` + Windows `.exe` / VST3) en Releases
- [ ] Headless Linux (Raspberry Pi) con ALSA/JACK
