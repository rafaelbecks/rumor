# Radial Proximity Tracker (RPi Installation Node)

Sistema de visión artificial en tiempo real diseñado para correr en **Raspberry Pi (OS 64-bit)**. La aplicación captura video desde una cámara web, detecta la presencia de participantes combinando **OpenCV** (detección de blobs por sustracción de fondo) y **MediaPipe** (tracking corporal), y calcula la aproximación radial hacia un objeto central translúcido impreso en 3D.

A medida que los participantes se acercan al centro, el sistema evalúa una curva de interpolación parametrizable y emite valores normalizados (`0.0` a `1.0`) vía **OSC (Open Sound Control)** para modular motores de audio y video en tiempo real.

---

## 1. Arquitectura del Sistema y Flujo de Datos

1. **Captura (`vision/camera.py`):** Hilo dedicado (threaded capture) para lectura de cámara USB mediante OpenCV (`CAP_V4L2`), evitando bloqueo por I/O y manteniendo baja latencia en Raspberry Pi (resolución objetivo: `640x480` @ `30fps`).
2. **Detección Híbrida (`vision/blob_detector.py` y `vision/mp_detector.py`):**
   - **Modo Blob (OpenCV):** Sustracción de fondo (`cv2.createBackgroundSubtractorMOG2`), filtrado morfológico, umbralización y extracción de contornos (`cv2.findContours`) con cálculo de momentos (`cv2.moments`) para obtener el centroide $(x, y)$ de cada sujeto.
   - **Modo Pose (MediaPipe):** Inferencia ligera (`PoseLandmarker` / `Solutions Pose` con `model_complexity=0`) para extraer el centro de masa (punto medio de caderas) cuando se requiere mayor robustez frente a sombras o reflejos de la pieza central.
   - **Máscara de Exclusión Central:** Máscara circular estática en $(C_x, C_y)$ para que la luminiscencia o refracción del objeto translúcido 3D no dispare falsos positivos de blobs en el centro.
3. **Geometría Radial y Curvas (`core/radial_mapper.py` y `core/curves.py`):**
   - Define el origen $(C_x, C_y)$ correspondiente a la ubicación física del objeto impreso en 3D dentro del encuadre.
   - Calcula la distancia euclidiana de cada sujeto al centro:
     $$d_i = \sqrt{(x_i - C_x)^2 + (y_i - C_y)^2}$$
   - Normaliza la distancia entre un radio máximo de entrada ($R_{max}$) y un radio mínimo de contacto ($R_{min}$):
     $$t = 1.0 - \text{clamp}\left(\frac{d - R_{min}}{R_{max} - R_{min}}, 0.0, 1.0\right)$$
   - Aplica una función de transferencia (curva lineal, exponencial, sigmoide o ease-in-out) sobre $t$ y un filtro **EMA (Exponential Moving Average)** para suavizar saltos bruscos de lectura y garantizar un *decay* orgánico hacia `0.0` cuando los participantes abandonan la zona.
4. **Comunicación (`network/osc_sender.py`):** Envío de paquetes UDP/OSC mediante `python-osc` hacia las IPs y puertos configurados.

---

## 2. Especificación de Mensajes OSC

El sistema emite a una tasa constante la siguiente estructura de direcciones OSC:

| Dirección OSC | Tipo | Rango | Descripción |
| :--- | :--- | :--- | :--- |
| `/installation/closest/norm` | `float` | `0.0` - `1.0` | Valor crudo normalizado del sujeto más cercano al centro (`0.0` = borde exterior, `1.0` = junto al objeto). |
| `/installation/closest/curve` | `float` | `0.0` - `1.0` | Valor procesado por la curva no lineal y suavizado (principal modulador de A/V). |
| `/installation/closest/angle` | `float` | `-180.0` - `180.0` | Ángulo de aproximación en grados ($\text{atan2}$) respecto al centro. |
| `/installation/crowd/count` | `int` | `0` - `N` | Cantidad de sujetos activos dentro del radio de detección ($R_{max}$). |
| `/installation/crowd/energy` | `float` | `0.0` - `1.0` | Promedio o suma ponderada de proximidad cuando múltiples personas rodean el objeto. |

---

## 3. Configuración (`config.yaml`)

Toda la calibración en sala se ajusta a través del archivo `config.yaml`:

```yaml
camera:
  device_id: 0
  width: 640
  height: 480
  fps: 30
  flip_horizontal: false

tracking:
  mode: "hybrid" # Opciones: "blob", "mediapipe", "hybrid"
  min_blob_area: 800
  max_blob_area: 45000
  bg_history: 500
  bg_var_threshold: 25
  ignore_center_mask_radius: 35

radial_zone:
  center_x: 320
  center_y: 240
  r_min: 40
  r_max: 220

response_curve:
  type: "exponential" # Opciones: "linear", "exponential", "sigmoid", "ease_in_out"
  exponent: 2.5
  ema_alpha: 0.18

osc:
  targets:
    - name: "audio_engine"
      ip: "127.0.0.1"
      port: 8000
    - name: "video_engine"
      ip: "192.168.1.100"
      port: 7000
```

---

## 4. Estructura del Proyecto

```text
radial-installation-tracker/
├── README.md
├── requirements.txt
├── config.yaml
├── main.py                  # Punto de entrada (--headless o --debug-gui)
├── vision/
│   ├── __init__.py
│   ├── camera.py            # Captura multihilo V4L2 con auto-reconexión
│   ├── blob_detector.py     # Pipeline OpenCV (MOG2 + morfología + contornos + máscara central)
│   └── mp_detector.py       # Pipeline MediaPipe optimizado para ARM64
├── core/
│   ├── __init__.py
│   ├── radial_mapper.py     # Cálculo polar (distancia, ángulo) y normalización
│   └── curves.py            # Funciones de transferencia y filtro EMA
└── network/
    ├── __init__.py
    └── osc_sender.py        # Despachador OSC multitarjet
```

---

## 5. Modos de Ejecución y Controles de Calibración

- **Modo Calibración (`python main.py --debug-gui`):**
  Abre una ventana de OpenCV mostrando la cámara en vivo, los anillos de $R_{min}$, $R_{max}$, la máscara de exclusión del objeto central, los vectores desde cada sujeto hacia el centro y un medidor visual con el valor actual de la curva.
  - `Click Izquierdo`: Redefine el centro $(C_x, C_y)$ donde está ubicado el objeto 3D.
  - `Tecla B`: Reinicia/recalibrar el modelo de fondo de OpenCV.
  - `Tecla S`: Guarda las coordenadas actuales en `config.yaml`.
  - `Tecla Q` / `ESC`: Cierra la aplicación.
- **Modo Producción / Instalación (`python main.py --headless`):**
  Desactiva todo el renderizado gráfico (`cv2.imshow`) para ahorrar recursos en la Raspberry Pi y corre enviando únicamente telemetría OSC.
