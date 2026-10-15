#pragma once

#include <cmath>
#include <cstdint>

namespace glow::midi_math {

inline float midiNoteToFrequency(float note) {
  return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
}

inline float frequencyWithCentsOffset(float frequencyHz, float cents) {
  return frequencyHz * std::pow(2.0f, cents / 1200.0f);
}

/** Map MIDI CC 0–127 → [0, 1]. */
inline float ccToNormalized(int value) {
  if (value <= 0) return 0.0f;
  if (value >= 127) return 1.0f;
  return static_cast<float>(value) / 127.0f;
}

/** Map normalized [0, 1] into [min, max]. */
inline float mapNormalized(float normalized, float minValue, float maxValue) {
  return minValue + (maxValue - minValue) * normalized;
}

/** Clamp to [min, max]. */
inline float clamp(float value, float minValue, float maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

/** Apply deadzone around 0 for bipolar [-1, 1] controls. */
inline float applyDeadzone(float bipolar, float deadzone) {
  const float dz = std::abs(deadzone);
  if (dz <= 0.0f) return bipolar;
  const float a = std::abs(bipolar);
  if (a <= dz) return 0.0f;
  const float sign = bipolar < 0.0f ? -1.0f : 1.0f;
  return sign * ((a - dz) / (1.0f - dz));
}

inline float invert01(float normalized, bool invert) {
  return invert ? (1.0f - normalized) : normalized;
}

inline float invertBipolar(float bipolar, bool invert) {
  return invert ? -bipolar : bipolar;
}

/** Optional shaping curves. Input/output stay in [0, 1]. */
inline float applyCurve(float normalized, const char* curve) {
  if (curve == nullptr || curve[0] == '\0') return normalized;
  // Exact-ish prefixes: "linear" (default), "exp", "log"
  if (curve[0] == 'e' && curve[1] == 'x') {  // exp
    constexpr float k = 3.0f;
    const float ek = std::exp(k);
    return (std::exp(k * normalized) - 1.0f) / (ek - 1.0f);
  }
  if (curve[0] == 'l' && curve[1] == 'o') {  // log (not "linear")
    constexpr float k = 3.0f;
    return std::log(1.0f + (std::exp(k) - 1.0f) * normalized) / k;
  }
  return normalized;  // linear / unknown
}

}  // namespace glow::midi_math
