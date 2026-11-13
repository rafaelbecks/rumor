# Radial Proximity Tracker — Versión Processing 4

Implementación completa y modular del **Radial Proximity Tracker** nativa para **Processing 4** (compatible con Windows, macOS, Linux y Raspberry Pi ARM64).

---

## 1. Librerías Requeridas en Processing

Antes de ejecutar el sketch, abre el IDE de Processing y ve a **Sketch → Import Library... → Manage Libraries...** e instala únicamente estas 2 librerías estándar:

1. **Video** (*The Processing Foundation*) — para captura de cámara USB.
2. **oscP5** (*Andreas Schlegel*) — para emisión de paquetes UDP/OSC multitarjet.

*(Nota: El motor de sustracción de fondo, filtrado morfológico, máscara de exclusión circular del objeto 3D y etiquetado de componentes conexos está implementado nativamente sobre el arreglo de píxeles en `BlobDetector.pde`, por lo que no depende de binarios nativos externos que suelen fallar en arquitecturas ARM64 o Processing 4).*

---

## 2. Estructura del Sketch

```text
processing/RadialProximityTracker/
├── RadialProximityTracker.pde   # Sketch principal, loop de captura, HUD y calibración
├── BlobDetector.pde             # Sustracción de fondo, morfología, máscara central 3D y blobs
├── RadialMapper.pde             # Cálculo polar (distancia, ángulo, normalización y energía)
├── Curves.pde                   # Funciones de transferencia y filtro EMA
├── OSCSender.pde                # Emisor OSC multitarjet (5 rutas OSC)
└── data/
    └── config.json              # Parámetros de calibración persistentes
```

---

## 3. Controles Interactivos en Vivo

- **`Click Izquierdo`**: Redefine en vivo la coordenada `(center_x, center_y)` del objeto central impreso en 3D.
- **`Tecla B`**: Reinicia/recalibra el modelo de fondo.
- **`Tecla S`**: Guarda la calibración actual en `data/config.json`.
- **`Tecla V`**: Alterna la vista entre la cámara en color y la máscara binaria de blobs.
- **`Tecla M`**: Activa/desactiva el **Modo Simulación con Mouse** (permite probar todas las curvas y el envío OSC moviendo el cursor hacia el centro aunque no haya personas frente a la cámara).
- **`Teclas 1, 2, 3, 4`**: Cambia en vivo el tipo de curva (`1`: `linear`, `2`: `exponential`, `3`: `sigmoid`, `4`: `ease_in_out`).
- **`Tecla G`**: Alterna entre modo gráfico de calibración (`DEBUG-GUI`) y modo ligero sin renderizado (`HEADLESS`).
