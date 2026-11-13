// ============================================================================
// RadialMapper.pde
// Cálculo polar (distancia euclidiana, ángulo en grados) y normalización radial.
// ============================================================================

class SubjectPolarMetrics {
  int x, y;
  float distance;
  float angleDeg;
  float normalizedProximity;
  boolean insideActiveZone;

  SubjectPolarMetrics(
    int x,
    int y,
    float distance,
    float angleDeg,
    float normalizedProximity,
    boolean insideActiveZone
  ) {
    this.x = x;
    this.y = y;
    this.distance = distance;
    this.angleDeg = angleDeg;
    this.normalizedProximity = normalizedProximity;
    this.insideActiveZone = insideActiveZone;
  }
}

class RadialFrameState {
  float closestNorm = 0.0;
  float closestAngle = 0.0;
  int crowdCount = 0;
  float crowdEnergy = 0.0;
  SubjectPolarMetrics closestSubject = null;
  ArrayList<SubjectPolarMetrics> subjects = new ArrayList<SubjectPolarMetrics>();
}

class RadialMapper {
  int centerX;
  int centerY;
  float rMin;
  float rMax;
  float lastClosestAngle = 0.0;

  RadialMapper(int centerX, int centerY, float rMin, float rMax) {
    this.centerX = centerX;
    this.centerY = centerY;
    this.rMin = rMin;
    this.rMax = max(rMax, rMin + 1.0);
  }

  void setCenter(int cx, int cy) {
    this.centerX = cx;
    this.centerY = cy;
  }

  float computeNormProximity(float distance) {
    float span = rMax - rMin;
    if (span <= 0.0001) {
      return (distance <= rMin) ? 1.0 : 0.0;
    }
    float ratio = (distance - rMin) / span;
    return 1.0 - constrain(ratio, 0.0, 1.0);
  }

  RadialFrameState evaluate(ArrayList<PVector> points) {
    RadialFrameState state = new RadialFrameState();
    ArrayList<SubjectPolarMetrics> activeSubjects = new ArrayList<SubjectPolarMetrics>();

    for (PVector pt : points) {
      int px = round(pt.x);
      int py = round(pt.y);
      float dx = px - centerX;
      float dy = py - centerY;
      float distance = sqrt(dx * dx + dy * dy);
      float angleDeg = degrees(atan2(dy, dx));
      boolean inside = (distance <= rMax);
      float normT = inside ? computeNormProximity(distance) : 0.0;

      SubjectPolarMetrics metrics = new SubjectPolarMetrics(
        px, py, distance, angleDeg, normT, inside
      );
      state.subjects.add(metrics);
      if (inside) {
        activeSubjects.add(metrics);
      }
    }

    state.crowdCount = activeSubjects.size();
    if (state.crowdCount == 0) {
      state.closestNorm = 0.0;
      state.closestAngle = lastClosestAngle;
      state.crowdEnergy = 0.0;
      return state;
    }

    SubjectPolarMetrics closest = activeSubjects.get(0);
    float sumProx = 0.0;
    for (SubjectPolarMetrics s : activeSubjects) {
      if (s.distance < closest.distance) {
        closest = s;
      }
      sumProx += s.normalizedProximity;
    }

    lastClosestAngle = closest.angleDeg;
    state.closestSubject = closest;
    state.closestNorm = closest.normalizedProximity;
    state.closestAngle = closest.angleDeg;

    float meanProx = sumProx / state.crowdCount;
    float multiPersonBoost = min(1.0, (state.crowdCount - 1) * 0.15);
    state.crowdEnergy = constrain(
      meanProx + (1.0 - meanProx) * multiPersonBoost * meanProx,
      0.0,
      1.0
    );

    return state;
  }
}
