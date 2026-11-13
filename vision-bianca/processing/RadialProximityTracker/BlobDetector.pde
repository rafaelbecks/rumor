// ============================================================================
// BlobDetector.pde
// Sustracción de fondo adaptativa, filtro morfológico (Open/Close),
// máscara circular negra central (objeto 3D) y etiquetado de componentes conexos.
// ============================================================================

class Blob {
  int cx, cy;
  int minX, minY, maxX, maxY;
  int area;

  Blob(int cx, int cy, int minX, int minY, int maxX, int maxY, int area) {
    this.cx = cx;
    this.cy = cy;
    this.minX = minX;
    this.minY = minY;
    this.maxX = maxX;
    this.maxY = maxY;
    this.area = area;
  }

  int width() {
    return max(1, maxX - minX);
  }

  int height() {
    return max(1, maxY - minY);
  }
}

class BlobDetector {
  int w, h;
  float minBlobArea;
  float maxBlobArea;
  float learningRate;
  int threshold;
  int ignoreCenterMaskRadius;

  float[] bgModelLuma;
  boolean bgInitialized = false;

  int[] rawMask;
  int[] tempMask;
  int[] morphMask;
  boolean[] visited;
  int[] queueX;
  int[] queueY;

  PImage fgMaskImage;

  BlobDetector(
    int w,
    int h,
    float minBlobArea,
    float maxBlobArea,
    float learningRate,
    int threshold,
    int ignoreCenterMaskRadius
  ) {
    this.w = w;
    this.h = h;
    this.minBlobArea = minBlobArea;
    this.maxBlobArea = maxBlobArea;
    this.learningRate = constrain(learningRate, 0.0001, 0.5);
    this.threshold = threshold;
    this.ignoreCenterMaskRadius = ignoreCenterMaskRadius;

    int totalPixels = w * h;
    bgModelLuma = new float[totalPixels];
    rawMask = new int[totalPixels];
    tempMask = new int[totalPixels];
    morphMask = new int[totalPixels];
    visited = new boolean[totalPixels];
    queueX = new int[totalPixels];
    queueY = new int[totalPixels];

    fgMaskImage = createImage(w, h, RGB);
  }

  void resetBackground() {
    bgInitialized = false;
  }

  ArrayList<Blob> detect(PImage frame, int centerX, int centerY) {
    ArrayList<Blob> blobs = new ArrayList<Blob>();
    if (frame == null || frame.width != w || frame.height != h) {
      return blobs;
    }

    frame.loadPixels();
    int total = w * h;

    // 1. Inicializar o actualizar sustracción de fondo en luminancia
    if (!bgInitialized) {
      for (int i = 0; i < total; i++) {
        int c = frame.pixels[i];
        int r = (c >> 16) & 0xFF;
        int g = (c >> 8) & 0xFF;
        int b = c & 0xFF;
        bgModelLuma[i] = 0.299f * r + 0.587f * g + 0.114f * b;
        rawMask[i] = 0;
      }
      bgInitialized = true;
      return blobs;
    }

    for (int i = 0; i < total; i++) {
      int c = frame.pixels[i];
      int r = (c >> 16) & 0xFF;
      int g = (c >> 8) & 0xFF;
      int b = c & 0xFF;
      float luma = 0.299f * r + 0.587f * g + 0.114f * b;

      float diff = abs(luma - bgModelLuma[i]);
      if (diff > threshold) {
        rawMask[i] = 1;
        // Aprendizaje más lento sobre primer plano para no absorber participantes rápido
        bgModelLuma[i] = (1.0f - learningRate * 0.15f) * bgModelLuma[i] + (learningRate * 0.15f) * luma;
      } else {
        rawMask[i] = 0;
        bgModelLuma[i] = (1.0f - learningRate) * bgModelLuma[i] + learningRate * luma;
      }
    }

    // 2. Filtro morfológico: Apertura (Erosión + Dilatación) y Cierre para limpiar ruido
    erode3x3(rawMask, tempMask);
    dilate3x3(tempMask, morphMask);
    dilate3x3(morphMask, tempMask);
    erode3x3(tempMask, morphMask);

    // 3. Regla crítica: Aplicar máscara circular negra en (centerX, centerY)
    // para bloquear los brillos y refracciones del objeto translúcido impreso en 3D
    int rSq = ignoreCenterMaskRadius * ignoreCenterMaskRadius;
    if (ignoreCenterMaskRadius > 0) {
      int minY = max(0, centerY - ignoreCenterMaskRadius);
      int maxY = min(h - 1, centerY + ignoreCenterMaskRadius);
      int minX = max(0, centerX - ignoreCenterMaskRadius);
      int maxX = min(w - 1, centerX + ignoreCenterMaskRadius);
      for (int y = minY; y <= maxY; y++) {
        int dy = y - centerY;
        int rowOffset = y * w;
        for (int x = minX; x <= maxX; x++) {
          int dx = x - centerX;
          if (dx * dx + dy * dy <= rSq) {
            morphMask[rowOffset + x] = 0;
          }
        }
      }
    }

    // Actualizar PImage de debug para visualizar la máscara (Tecla V)
    fgMaskImage.loadPixels();
    for (int i = 0; i < total; i++) {
      fgMaskImage.pixels[i] = (morphMask[i] == 1) ? color(255) : color(0);
      visited[i] = false;
    }
    fgMaskImage.updatePixels();

    // 4. Extracción de contornos/blobs por búsqueda en anchura (BFS) en grilla (paso 2px para velocidad ARM64)
    for (int y = 1; y < h - 1; y += 2) {
      int rowOffset = y * w;
      for (int x = 1; x < w - 1; x += 2) {
        int idx = rowOffset + x;
        if (morphMask[idx] == 1 && !visited[idx]) {
          int head = 0;
          int tail = 0;

          queueX[tail] = x;
          queueY[tail] = y;
          tail++;
          visited[idx] = true;

          long sumX = 0;
          long sumY = 0;
          int count = 0;
          int bMinX = x, bMaxX = x;
          int bMinY = y, bMaxY = y;

          while (head < tail) {
            int cx = queueX[head];
            int cy = queueY[head];
            head++;

            sumX += cx;
            sumY += cy;
            count++;

            if (cx < bMinX) bMinX = cx;
            if (cx > bMaxX) bMaxX = cx;
            if (cy < bMinY) bMinY = cy;
            if (cy > bMaxY) bMaxY = cy;

            // Vecinos 4-conexos
            int[] nxList = {cx - 1, cx + 1, cx, cx};
            int[] nyList = {cy, cy, cy - 1, cy + 1};
            for (int k = 0; k < 4; k++) {
              int nx = nxList[k];
              int ny = nyList[k];
              if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                int nIdx = ny * w + nx;
                if (morphMask[nIdx] == 1 && !visited[nIdx]) {
                  visited[nIdx] = true;
                  queueX[tail] = nx;
                  queueY[tail] = ny;
                  tail++;
                }
              }
            }
          }

          if (count >= minBlobArea && count <= maxBlobArea) {
            int centroidX = (int) (sumX / count);
            int centroidY = (int) (sumY / count);
            blobs.add(new Blob(centroidX, centroidY, bMinX, bMinY, bMaxX, bMaxY, count));
          }
        }
      }
    }

    return blobs;
  }

  void erode3x3(int[] src, int[] dst) {
    for (int i = 0; i < w * h; i++) dst[i] = 0;
    for (int y = 1; y < h - 1; y++) {
      int row = y * w;
      for (int x = 1; x < w - 1; x++) {
        int i = row + x;
        if (src[i] == 1 &&
            src[i - 1] == 1 && src[i + 1] == 1 &&
            src[i - w] == 1 && src[i + w] == 1) {
          dst[i] = 1;
        }
      }
    }
  }

  void dilate3x3(int[] src, int[] dst) {
    for (int i = 0; i < w * h; i++) dst[i] = 0;
    for (int y = 1; y < h - 1; y++) {
      int row = y * w;
      for (int x = 1; x < w - 1; x++) {
        int i = row + x;
        if (src[i] == 1 ||
            src[i - 1] == 1 || src[i + 1] == 1 ||
            src[i - w] == 1 || src[i + w] == 1) {
          dst[i] = 1;
        }
      }
    }
  }
}
