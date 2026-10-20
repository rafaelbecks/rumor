#pragma once

#include "audio/Envelope.h"
#include "audio/Filter.h"
#include "audio/Granular.h"
#include "audio/Limiter.h"
#include "audio/Oscillator.h"
#include "audio/WaveFolder.h"
#include "control/PerformanceState.h"
#include "midi/MidiMapper.h"
#include "parameters/ParameterRegistry.h"
#include "util/LockFreeQueue.h"

#include <array>
#include <atomic>

namespace glow {

/**
 * Synth voice + FX. Chord spread adds scale tones (or harmonic partials) from the root.
 * DSP knows nothing about MIDI CC numbers.
 */
class Synth {
 public:
  static constexpr int kMaxChordExtras = 6;  // 3rd/5th/7th × up+down

  void prepare(float sampleRate, int maxGrains);
  void setParameterRegistry(ParameterRegistry* registry) { registry_ = registry; }
  void setPerformance(PerformanceState* performance) { performance_ = performance; }
  void setNoteQueue(LockFreeQueue<NoteEvent, 256>* queue) { noteQueue_ = queue; }

  void drainNotes();
  void process(float* outL, float* outR, int numFrames);

  void noteOn(uint8_t note, uint8_t velocity);
  void noteOff(uint8_t note);

  /** Extra tones mixed with the drone before the shared filter / folder / grains. 1..3. */
  static constexpr int kMaxPitches = 3;
  void setPitchCount(int count);
  void setExtraNote(int index, int note);

 private:
  void pullParameters();

  ParameterRegistry* registry_{nullptr};
  PerformanceState* performance_{nullptr};
  LockFreeQueue<NoteEvent, 256>* noteQueue_{nullptr};

  float sampleRate_{48000.0f};
  int currentNote_{-1};
  float fundamentalHz_{440.0f};
  float velocity_{0.0f};
  float amplitude_{1.0f};
  float chordSpread_{0.0f};
  float centerFreq_{440.0f};
  float chordWeight_{0.0f};
  Waveform waveform_{Waveform::Saw};
  int armedExtras_{0};

  Oscillator osc_;
  std::array<Oscillator, kMaxPitches - 1> extraOsc_{};
  std::atomic<int> pitchCount_{1};
  std::array<std::atomic<int>, kMaxPitches - 1> extraNote_{};
  std::array<Oscillator, kMaxChordExtras> chordOsc_{};
  std::array<float, kMaxChordExtras> chordAmp_{};
  Envelope env_;
  WaveFolder folder_;
  Filter filter_;
  Granular granular_;
  Limiter limiter_;
};

}  // namespace glow
