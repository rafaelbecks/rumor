#pragma once

namespace glow {

/**
 * Lightweight wavefolder — folds when driven past ±1 (with optional DC bias).
 * Placed after the voice, before the filter.
 */
class WaveFolder {
 public:
  void setDrive(float drive01);
  void setSymmetry(float bipolar);  // -1..1 DC offset into the folder
  void setMix(float mix01);
  float process(float input);

 private:
  float drive_{0.0f};
  float symmetry_{0.0f};
  float mix_{1.0f};
};

}  // namespace glow
