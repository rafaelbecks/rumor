#include "audio/Envelope.h"

#include "util/MidiMath.h"

namespace glow {

void Envelope::setSampleRate(float sampleRate) {
  sampleRate_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
}

void Envelope::setAttack(float seconds) {
  const float s = midi_math::clamp(seconds, 0.0005f, 10.0f);
  attackInc_ = 1.0f / (s * sampleRate_);
}

void Envelope::setRelease(float seconds) {
  const float s = midi_math::clamp(seconds, 0.0005f, 20.0f);
  releaseInc_ = 1.0f / (s * sampleRate_);
}

void Envelope::noteOn(float velocity01) {
  peak_ = midi_math::clamp(velocity01, 0.0f, 1.0f);
  state_ = State::Attack;
}

void Envelope::noteOff() {
  if (state_ != State::Idle) state_ = State::Release;
}

float Envelope::process() {
  switch (state_) {
    case State::Idle:
      level_ = 0.0f;
      break;
    case State::Attack:
      level_ += attackInc_ * peak_;
      if (level_ >= peak_) {
        level_ = peak_;
        state_ = State::Sustain;
      }
      break;
    case State::Sustain:
      level_ = peak_;
      break;
    case State::Release:
      level_ -= releaseInc_ * peak_;
      if (level_ <= 0.0f) {
        level_ = 0.0f;
        state_ = State::Idle;
      }
      break;
  }
  return level_;
}

}  // namespace glow
