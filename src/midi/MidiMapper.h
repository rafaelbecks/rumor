#pragma once

#include "config/ConfigLoader.h"
#include "control/Joystick.h"
#include "control/PerformanceState.h"
#include "midi/MidiMessage.h"
#include "parameters/ParameterRegistry.h"
#include "util/LockFreeQueue.h"

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace glow {

struct NoteEvent {
  enum class Type : uint8_t { NoteOn, NoteOff };
  Type type{Type::NoteOn};
  uint8_t note{60};
  uint8_t velocity{100};
};

/**
 * Maps MIDI messages → parameters / joystick / note events / button actions.
 * Knows nothing about oscillators or filters.
 */
class MidiMapper {
 public:
  using NoteHandler = std::function<void(const NoteEvent&)>;

  void configure(const MidiConfig& midi, const JoystickConfig& joystick);
  void setRegistry(ParameterRegistry* registry);
  void setJoystick(Joystick* joystick);
  void setPerformance(PerformanceState* performance) { performance_ = performance; }
  void setNoteQueue(LockFreeQueue<NoteEvent, 256>* queue) { noteQueue_ = queue; }
  void setVerbose(bool verbose) { verbose_ = verbose; }

  void handleMessage(const MidiMessage& msg);

  /** Drive joystick smoothing from the control thread. */
  void updateControls();

 private:
  void handleCc(uint8_t cc, uint8_t value);
  void handleButtonAction(const ButtonMapping& btn);
  void cycleWaveform();
  void cycleScale();
  void setPitchFromFifths(int fifths);

  MidiConfig midi_{};
  JoystickConfig joystickCfg_{};
  ParameterRegistry* registry_{nullptr};
  Joystick* joystick_{nullptr};
  PerformanceState* performance_{nullptr};
  LockFreeQueue<NoteEvent, 256>* noteQueue_{nullptr};
  std::unordered_map<int, bool> buttonDown_;
  int soundingNote_{-1};
  bool verbose_{false};
};

}  // namespace glow
