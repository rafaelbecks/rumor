#pragma once

#include <cstdint>

namespace glow {

enum class MidiMessageType : uint8_t {
  NoteOn,
  NoteOff,
  ControlChange
};

struct MidiMessage {
  MidiMessageType type{MidiMessageType::NoteOn};
  uint8_t channel{0};   // 0–15
  uint8_t data1{0};     // note or CC number
  uint8_t data2{0};     // velocity or CC value
};

}  // namespace glow
