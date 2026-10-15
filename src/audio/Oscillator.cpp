#include "audio/Oscillator.h"

#include "util/MidiMath.h"

namespace glow {

void Oscillator::setSampleRate(float sampleRate) {
  sampleRate_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
}

void Oscillator::setFrequency(float frequencyHz) {
  frequency_ = frequencyHz;
}

void Oscillator::setWaveform(Waveform waveform) {
  waveformPos_ = waveformToParam(waveform);
}

void Oscillator::setWaveformPosition(float position) {
  waveformPos_ = midi_math::clamp(position, 0.0f, static_cast<float>(kWaveformCount - 1));
}

void Oscillator::setSineTimbre(float timbre) {
  sineTimbre_ = midi_math::clamp(timbre, 0.0f, 1.0f);
}

void Oscillator::reset() {
  phase_ = 0.0f;
}

float Oscillator::process() {
  const float sample = morphWaveformSample(waveformPos_, phase_, sineTimbre_);

  phase_ += frequency_ / sampleRate_;
  if (phase_ >= 1.0f) phase_ -= 1.0f;
  if (phase_ < 0.0f) phase_ += 1.0f;
  return sample;
}

}  // namespace glow
