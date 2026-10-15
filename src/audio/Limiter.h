#pragma once

namespace glow {

/** Cheap peak safety limiter — not a mastering brickwall. */
class Limiter {
 public:
  void setCeiling(float linearCeiling);
  float process(float input);

 private:
  float ceiling_{0.95f};
  float env_{0.0f};
};

}  // namespace glow
