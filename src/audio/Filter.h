#pragma once

namespace glow {

/**
 * Moog-style 4-pole resonant ladder LPF (24 dB/oct).
 * Lightweight transcendental approximation suitable for real-time / Pi.
 */
class Filter {
 public:
  void setSampleRate(float sampleRate);
  void setCutoff(float hz);
  void setResonance(float amount01);
  float process(float input);
  void reset();

 private:
  void updateCoeffs();

  float sampleRate_{48000.0f};
  float cutoff_{4000.0f};
  float resonance_{0.35f};
  float g_{0.5f};       // stage gain from cutoff
  float drive_{1.0f};   // input drive into tanh
  float stage_[4]{};    // ladder integrator states
  float delay_[4]{};    // unit delays for bilinear stages
};

}  // namespace glow
