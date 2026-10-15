#pragma once

#include "util/Scale.h"

#include <atomic>

namespace glow {

/** Shared musical context for pitch quantize + chord voicing (control ↔ DSP). */
struct PerformanceState {
  void setRoot(int midiNote) {
    rootMidi_.store(midiNote, std::memory_order_relaxed);
  }

  void setBaseRoot(int midiNote) {
    baseRootMidi_.store(midiNote, std::memory_order_relaxed);
  }

  void setScale(ScaleId scale) {
    scaleId_.store(static_cast<int>(scale), std::memory_order_relaxed);
  }

  int root() const { return rootMidi_.load(std::memory_order_relaxed); }
  int baseRoot() const { return baseRootMidi_.load(std::memory_order_relaxed); }

  ScaleId scale() const {
    return static_cast<ScaleId>(scaleId_.load(std::memory_order_relaxed));
  }

  /** Serial pitch mode: control thread publishes a MIDI note; DSP reads it. */
  void setSerialMidi(float midiNote) {
    serialMidi_.store(midiNote, std::memory_order_relaxed);
    serialPitchActive_.store(true, std::memory_order_relaxed);
  }

  /** Stop serial from overriding note-on / drone pitch (e.g. after disconnect or manual drone). */
  void clearSerialPitch() {
    serialPitchActive_.store(false, std::memory_order_relaxed);
  }

  bool serialPitchActive() const {
    return serialPitchActive_.load(std::memory_order_relaxed);
  }

  float serialMidi() const { return serialMidi_.load(std::memory_order_relaxed); }

 private:
  std::atomic<int> rootMidi_{60};
  std::atomic<int> baseRootMidi_{60};
  std::atomic<int> scaleId_{static_cast<int>(ScaleId::Off)};
  std::atomic<float> serialMidi_{60.0f};
  std::atomic<bool> serialPitchActive_{false};
};

}  // namespace glow
