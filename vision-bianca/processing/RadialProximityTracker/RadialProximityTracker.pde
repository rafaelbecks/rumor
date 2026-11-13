// ============================================================================
// RadialProximityTracker.pde
// Versión nativa para Processing 4.x (PC / Mac / Raspberry Pi ARM64)
// Librerías requeridas (Sketch -> Import Library -> Manage Libraries):
//   1. Video (The Processing Foundation)
//   2. oscP5 (Andreas Schlegel)
// ============================================================================

import processing.video.*;
import oscP5.*;
import netP5.*;

// --- Configuración global ---
JSONObject config;
String configPath = "data/config.json";

// --- Cámara y Reconexión ---
Capture cam;
int camDeviceId = 3;
int camWidth = 640;
int camHeight = 480;
int camFps = 30;
boolean flipHorizontal = false;
boolean camConnected = false;
int lastReconnectAttemptMs = 0;
int reconnectDelayMs = 2000;
PImage currentFrame;

// --- Módulos del sistema ---
BlobDetector blobDetector;
RadialMapper radialMapper;
EMAFilter curveEMA;
EMAFilter energyEMA;
OSCSender oscSender;

// --- Parámetros de curva y visualización ---
String curveType = "exponential";
float curveExponent = 2.5;
float emaAlpha = 0.18;
int ignoreCenterMaskRadius = 35;

boolean showDebugGUI = true;
boolean showFgMask = false;
boolean mouseSimMode = false; // Permite probar con el mouse (Tecla M) si no hay sujetos

final int HUD_WIDTH = 165;

void settings() {
  // Cargamos dimensiones desde config.json antes de abrir la ventana
  File f = new File(sketchPath(configPath));
  if (f.exists()) {
    config = loadJSONObject(configPath);
    if (config != null && config.hasKey("camera")) {
      JSONObject camCfg = config.getJSONObject("camera");
      camWidth = camCfg.getInt("width", 640);
      camHeight = camCfg.getInt("height", 480);
    }
  }
  size(camWidth, camHeight);
}

void setup() {
  surface.setTitle("Radial Proximity Tracker - Processing Node");
  loadConfiguration();
  frameRate(camFps);

  currentFrame = createImage(camWidth, camHeight, RGB);

  // 1. Inicializar Detector de Blobs con máscara central
  JSONObject trackCfg = config.getJSONObject("tracking");
  blobDetector = new BlobDetector(
    camWidth,
    camHeight,
    trackCfg.getFloat("min_blob_area", 800),
    trackCfg.getFloat("max_blob_area", 45000),
    trackCfg.getFloat("bg_learning_rate", 0.005),
    trackCfg.getInt("bg_threshold", 32),
    ignoreCenterMaskRadius
  );

  // 2. Inicializar Geometría Radial
  JSONObject radialCfg = config.getJSONObject("radial_zone");
  radialMapper = new RadialMapper(
    radialCfg.getInt("center_x", camWidth / 2),
    radialCfg.getInt("center_y", camHeight / 2),
    radialCfg.getFloat("r_min", 40),
    radialCfg.getFloat("r_max", 220)
  );

  // 3. Inicializar Filtros EMA
  curveEMA = new EMAFilter(emaAlpha, 0.0);
  energyEMA = new EMAFilter(emaAlpha, 0.0);

  // 4. Inicializar Despachador OSC
  JSONObject oscCfg = config.getJSONObject("osc");
  oscSender = new OSCSender(this, oscCfg.getJSONArray("targets"));

  // 5. Iniciar Cámara
  initCamera();
}

void loadConfiguration() {
  File f = new File(sketchPath(configPath));
  if (!f.exists()) {
    println("[WARN] No se encontró data/config.json, usando valores por defecto.");
    config = new JSONObject();
    return;
  }
  config = loadJSONObject(configPath);

  JSONObject camCfg = config.getJSONObject("camera");
  camDeviceId = camCfg.getInt("device_id", 0);
  camWidth = camCfg.getInt("width", 640);
  camHeight = camCfg.getInt("height", 480);
  camFps = camCfg.getInt("fps", 30);
  flipHorizontal = camCfg.getBoolean("flip_horizontal", false);

  JSONObject trackCfg = config.getJSONObject("tracking");
  ignoreCenterMaskRadius = trackCfg.getInt("ignore_center_mask_radius", 35);

  JSONObject curveCfg = config.getJSONObject("response_curve");
  curveType = curveCfg.getString("type", "exponential");
  curveExponent = curveCfg.getFloat("exponent", 2.5);
  emaAlpha = curveCfg.getFloat("ema_alpha", 0.18);
}

void saveConfiguration() {
  JSONObject radialCfg = config.getJSONObject("radial_zone");
  radialCfg.setInt("center_x", radialMapper.centerX);
  radialCfg.setInt("center_y", radialMapper.centerY);
  radialCfg.setFloat("r_min", radialMapper.rMin);
  radialCfg.setFloat("r_max", radialMapper.rMax);

  JSONObject curveCfg = config.getJSONObject("response_curve");
  curveCfg.setString("type", curveType);
  curveCfg.setFloat("exponent", curveExponent);
  curveCfg.setFloat("ema_alpha", emaAlpha);

  saveJSONObject(config, configPath);
  println("[INFO] Calibración guardada en " + configPath + " -> center=(" + radialMapper.centerX + ", " + radialMapper.centerY + ")");
}

void initCamera() {
  try {
    if (cam != null) {
      cam.stop();
      cam = null;
    }
    String[] cameras = Capture.list();
    if (cameras == null || cameras.length == 0) {
      println("[WARN] No se detectaron cámaras USB disponibles.");
      camConnected = false;
      return;
    }

    int idx = constrain(camDeviceId, 0, cameras.length - 1);
    println("[INFO] Abriendo cámara [" + idx + "]: " + cameras[idx]);
    cam = new Capture(this, camWidth, camHeight, cameras[idx], camFps);
    cam.start();
    camConnected = true;
  } catch (Exception e) {
    println("[ERROR] Fallo al iniciar la cámara: " + e.getMessage());
    camConnected = false;
  }
}

void draw() {
  background(18);

  // 1. Lectura de frame o reconexión resiliente cada 2 segundos
  boolean hasValidFrame = false;
  if (cam != null && camConnected) {
    if (cam.available()) {
      cam.read();
    }
    if (cam.width > 0 && cam.height > 0) {
      if (flipHorizontal) {
        pushMatrix();
        scale(-1, 1);
        currentFrame.copy(cam, 0, 0, cam.width, cam.height, 0, 0, camWidth, camHeight);
        popMatrix();
        mirrorPImageHorizontal(currentFrame);
      } else {
        currentFrame.copy(cam, 0, 0, cam.width, cam.height, 0, 0, camWidth, camHeight);
      }
      hasValidFrame = true;
    }
  } else {
    if (millis() - lastReconnectAttemptMs >= reconnectDelayMs) {
      lastReconnectAttemptMs = millis();
      println("[WARN] Reintentando conexión con cámara USB...");
      initCamera();
    }
  }

  // 2. Detección de sujetos (Blobs + Máscara de exclusión central)
  ArrayList<PVector> detectedCentroids = new ArrayList<PVector>();
  ArrayList<Blob> detectedBlobs = new ArrayList<Blob>();

  if (hasValidFrame) {
    detectedBlobs = blobDetector.detect(currentFrame, radialMapper.centerX, radialMapper.centerY);
    for (Blob b : detectedBlobs) {
      detectedCentroids.add(new PVector(b.cx, b.cy));
    }
  }

  // Si está activo el modo de prueba con Mouse (Tecla M), agrega el cursor como sujeto
  if (mouseSimMode && mouseX < width - HUD_WIDTH) {
    detectedCentroids.add(new PVector(mouseX, mouseY));
  }

  // 3. Evaluación geométrica polar (distancia, ángulo, normalización y energía grupal)
  RadialFrameState radialState = radialMapper.evaluate(detectedCentroids);

  // 4. Evaluación de curva de transferencia y filtro EMA
  // Regla crítica: cuando crowdCount == 0, el objetivo pasa a 0.0 y la salida cae suavemente por el filtro EMA
  float targetCurve = 0.0;
  float targetEnergy = 0.0;
  if (radialState.crowdCount > 0) {
    targetCurve = evaluateCurve(radialState.closestNorm, curveType, curveExponent);
    targetEnergy = radialState.crowdEnergy;
  }

  float smoothedCurveVal = curveEMA.update(targetCurve);
  float smoothedEnergyVal = energyEMA.update(targetEnergy);

  // 5. Emitir telemetría OSC a todos los destinos configurados
  oscSender.sendTelemetry(
    radialState.closestNorm,
    smoothedCurveVal,
    radialState.closestAngle,
    radialState.crowdCount,
    smoothedEnergyVal
  );

  // 6. Renderizado de Calibración (si showDebugGUI está activo)
  if (showDebugGUI) {
    renderDebugGUI(hasValidFrame, detectedBlobs, radialState, smoothedCurveVal, smoothedEnergyVal);
  } else {
    fill(0, 220, 120);
    textAlign(CENTER, CENTER);
    textSize(14);
    text("MODO HEADLESS ACTIVO (Render desactivado para ahorrar CPU)\nPresiona [G] para volver a la vista de calibración", width / 2, height / 2);
  }
}

void renderDebugGUI(
  boolean hasValidFrame,
  ArrayList<Blob> blobs,
  RadialFrameState state,
  float smoothedCurve,
  float smoothedEnergy
) {
  // Mostrar imagen de cámara o máscara binaria (Tecla V)
  if (showFgMask) {
    image(blobDetector.fgMaskImage, 0, 0);
  } else if (hasValidFrame) {
    image(currentFrame, 0, 0);
  } else {
    fill(30);
    noStroke();
    rect(0, 0, width, height);
    fill(255, 110, 40);
    textAlign(CENTER, CENTER);
    textSize(15);
    text("Esperando cámara USB... (Puedes presionar [M] para simular con el mouse)", (width - HUD_WIDTH) / 2, height / 2);
  }

  int cx = radialMapper.centerX;
  int cy = radialMapper.centerY;
  float rMin = radialMapper.rMin;
  float rMax = radialMapper.rMax;

  // 1. Área enmascarada del objeto 3D central (gris translúcido + borde gris)
  if (ignoreCenterMaskRadius > 0) {
    fill(110, 110, 110, 130);
    stroke(170, 170, 170, 220);
    strokeWeight(1.5);
    ellipse(cx, cy, ignoreCenterMaskRadius * 2, ignoreCenterMaskRadius * 2);
  }

  // 2. Anillo exterior r_max (verde) y anillo interior r_min (rojo/magenta)
  noFill();
  stroke(0, 220, 0);
  strokeWeight(2);
  ellipse(cx, cy, rMax * 2, rMax * 2);

  stroke(255, 0, 180);
  strokeWeight(2);
  ellipse(cx, cy, rMin * 2, rMin * 2);

  // Cruz central del objeto 3D
  stroke(255);
  strokeWeight(1);
  line(cx - 8, cy, cx + 8, cy);
  line(cx, cy - 8, cx, cy + 8);

  // Etiquetas de los anillos
  textAlign(CENTER, BOTTOM);
  textSize(11);
  fill(0, 230, 0);
  text("R_max (" + int(rMax) + "px)", cx, max(14, cy - rMax - 5));
  fill(255, 60, 200);
  text("R_min (" + int(rMin) + "px)", cx, max(14, cy - rMin - 5));

  // 3. Bounding boxes de los blobs detectados
  noFill();
  stroke(100, 190, 100, 170);
  strokeWeight(1);
  for (Blob b : blobs) {
    rect(b.minX, b.minY, b.width(), b.height());
  }

  // 4. Vectores desde cada sujeto hasta el centro con su distancia y valor normalizado
  textAlign(LEFT, BOTTOM);
  textSize(11);
  for (SubjectPolarMetrics subj : state.subjects) {
    boolean isClosest = (state.closestSubject != null && subj == state.closestSubject && subj.insideActiveZone);
    if (subj.insideActiveZone) {
      if (isClosest) {
        stroke(255, 255, 0);
        fill(255, 255, 0);
        strokeWeight(2.2);
      } else {
        stroke(0, 210, 255);
        fill(0, 210, 255);
        strokeWeight(1.3);
      }
    } else {
      stroke(130);
      fill(130);
      strokeWeight(1.0);
    }

    line(subj.x, subj.y, cx, cy);
    noStroke();
    ellipse(subj.x, subj.y, 10, 10);

    String lbl = "d:" + nf(subj.distance, 0, 0) + "px | t:" + nf(subj.normalizedProximity, 1, 2);
    text(lbl, subj.x + 8, subj.y - 6);
  }

  // 5. HUD Lateral
  int hudX = width - HUD_WIDTH;
  noStroke();
  fill(18, 18, 22, 205);
  rect(hudX, 0, HUD_WIDTH, height);

  int tx = hudX + 12;
  textAlign(LEFT, TOP);

  fill(0, 255, 130);
  textSize(14);
  text("FPS: " + nf(frameRate, 0, 1), tx, 12);

  fill(210);
  textSize(11);
  text("Curve: " + curveType, tx, 36);
  text("Mask : " + (showFgMask ? "FG MASK" : "RGB CAM"), tx, 52);
  text("Sim M: " + (mouseSimMode ? "ON" : "OFF"), tx, 68);

  fill(255);
  textSize(12);
  text("Norm : " + nf(state.closestNorm, 1, 3), tx, 94);
  fill(0, 220, 255);
  text("Curve: " + nf(smoothedCurve, 1, 3), tx, 114);
  fill(205);
  text("Angle: " + nf(state.closestAngle, 0, 1) + "°", tx, 134);
  text("Count: " + state.crowdCount, tx, 154);
  text("Enrgy: " + nf(smoothedEnergy, 1, 3), tx, 174);

  // Barra de progreso vertical representando el valor suavizado `curve`
  int barX = hudX + 30;
  int barW = 42;
  int barTop = 205;
  int barBottom = height - 48;
  int barH = max(10, barBottom - barTop);

  noFill();
  stroke(100);
  strokeWeight(1);
  rect(barX, barTop, barW, barH);

  int fillH = round(constrain(smoothedCurve, 0.0, 1.0) * barH);
  if (fillH > 0) {
    noStroke();
    float rCol = 255 * smoothedCurve;
    float gCol = 220 * (1.0 - 0.5 * smoothedCurve);
    float bCol = 255;
    fill(rCol, gCol, bCol);
    rect(barX + 1, barBottom - fillH, barW - 1, fillH);
  }

  // Marcador horizontal blanco del valor crudo `norm`
  int normY = barBottom - round(constrain(state.closestNorm, 0.0, 1.0) * barH);
  stroke(255);
  strokeWeight(2);
  line(barX - 5, normY, barX + barW + 5, normY);

  fill(0, 220, 255);
  textAlign(CENTER, TOP);
  textSize(11);
  text("CURVE", barX + barW / 2, barBottom + 6);

  // Leyenda inferior de controles
  fill(15, 15, 15, 180);
  noStroke();
  rect(0, height - 24, width - HUD_WIDTH, 24);
  fill(230);
  textAlign(LEFT, CENTER);
  textSize(11);
  text("[Click] Centro | [B] Reset BG | [S] Guardar | [V] Ver Máscara | [M] Sim Mouse | [1-4] Curva", 8, height - 12);
}

// --- Controles de Calibración Interactiva ---

void mousePressed() {
  if (mouseButton == LEFT && mouseX < width - HUD_WIDTH) {
    radialMapper.setCenter(mouseX, mouseY);
    println("[INFO] Nuevo centro radial establecido en vivo: (" + mouseX + ", " + mouseY + ")");
  }
}

void keyPressed() {
  if (key == 'b' || key == 'B') {
    blobDetector.resetBackground();
    println("[INFO] Modelo de fondo reiniciado manualmente (Tecla B).");
  } else if (key == 's' || key == 'S') {
    saveConfiguration();
  } else if (key == 'v' || key == 'V') {
    showFgMask = !showFgMask;
  } else if (key == 'm' || key == 'M') {
    mouseSimMode = !mouseSimMode;
    println("[INFO] Simulación con mouse: " + (mouseSimMode ? "ACTIVADA" : "DESACTIVADA"));
  } else if (key == 'g' || key == 'G') {
    showDebugGUI = !showDebugGUI;
  } else if (key == '1') {
    curveType = "linear";
  } else if (key == '2') {
    curveType = "exponential";
  } else if (key == '3') {
    curveType = "sigmoid";
  } else if (key == '4') {
    curveType = "ease_in_out";
  }
}

void mirrorPImageHorizontal(PImage img) {
  img.loadPixels();
  int w = img.width;
  int h = img.height;
  for (int y = 0; y < h; y++) {
    int rowStart = y * w;
    for (int x = 0; x < w / 2; x++) {
      int leftIdx = rowStart + x;
      int rightIdx = rowStart + (w - 1 - x);
      int tmp = img.pixels[leftIdx];
      img.pixels[leftIdx] = img.pixels[rightIdx];
      img.pixels[rightIdx] = tmp;
    }
  }
  img.updatePixels();
}
