#pragma once

namespace glow {

class Envelope {
 public:
  void setSampleRate(float sampleRate);
  void setAttack(float seconds);
  void setRelease(float seconds);
  void noteOn(float velocity01);
  void noteOff();
  float process();
  bool isActive() const { return state_ != State::Idle; }

 private:
  enum class State { Idle, Attack, Sustain, Release };
  float sampleRate_{48000.0f};
  float attackInc_{0.01f};
  float releaseInc_{0.01f};
  float level_{0.0f};
  float peak_{0.0f};
  State state_{State::Idle};
};

}  // namespace glow
