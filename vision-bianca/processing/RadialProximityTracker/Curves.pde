// ============================================================================
// Curves.pde
// Funciones de transferencia (linear, exponential, sigmoid, ease_in_out)
// y filtro EMA (Exponential Moving Average) con caída suave hacia 0.0.
// ============================================================================

float curveLinear(float t) {
  return constrain(t, 0.0, 1.0);
}

float curveExponential(float t, float exponent) {
  float tc = constrain(t, 0.0, 1.0);
  float exp = max(0.01, exponent);
  return constrain(pow(tc, exp), 0.0, 1.0);
}

float curveSigmoid(float t, float steepness) {
  float tc = constrain(t, 0.0, 1.0);
  float k = max(0.1, steepness);
  float raw = 1.0 / (1.0 + exp(-k * (tc - 0.5)));
  float minVal = 1.0 / (1.0 + exp(k * 0.5));
  float maxVal = 1.0 / (1.0 + exp(-k * 0.5));
  return constrain((raw - minVal) / (maxVal - minVal), 0.0, 1.0);
}

float curveEaseInOut(float t) {
  float tc = constrain(t, 0.0, 1.0);
  if (tc < 0.5) {
    return 4.0 * tc * tc * tc;
  }
  return 1.0 - pow(-2.0 * tc + 2.0, 3.0) / 2.0;
}

float evaluateCurve(float t, String curveType, float exponent) {
  String ctype = (curveType != null) ? curveType.trim().toLowerCase() : "exponential";
  if (ctype.equals("linear")) {
    return curveLinear(t);
  } else if (ctype.equals("exponential")) {
    return curveExponential(t, exponent);
  } else if (ctype.equals("sigmoid")) {
    float steepness = max(4.0, exponent * 4.0);
    return curveSigmoid(t, steepness);
  } else if (ctype.equals("ease_in_out")) {
    return curveEaseInOut(t);
  }
  return curveExponential(t, exponent);
}

class EMAFilter {
  float alpha;
  float value;

  EMAFilter(float alpha, float initialValue) {
    this.alpha = constrain(alpha, 0.001, 1.0);
    this.value = constrain(initialValue, 0.0, 1.0);
  }

  float update(float targetValue) {
    float target = constrain(targetValue, 0.0, 1.0);
    value = (alpha * target) + ((1.0 - alpha) * value);
    if (value < 0.00001 && target == 0.0) {
      value = 0.0;
    }
    return value;
  }

  void reset(float val) {
    value = constrain(val, 0.0, 1.0);
  }
}
