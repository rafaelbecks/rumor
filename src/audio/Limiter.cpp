#include "audio/Limiter.h"

#include <cmath>

namespace glow {

void Limiter::setCeiling(float linearCeiling) {
  ceiling_ = linearCeiling > 0.1f ? linearCeiling : 0.95f;
}

float Limiter::process(float input) {
  const float a = std::fabs(input);
  // Fast attack / slower release envelope
  if (a > env_) {
    env_ = a;
  } else {
    env_ = env_ * 0.9995f + a * 0.0005f;
  }
  float gain = 1.0f;
  if (env_ > ceiling_) {
    gain = ceiling_ / env_;
  }
  return input * gain;
}

}  // namespace glow
