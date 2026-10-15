#include "midi/MidiMapper.h"

#include "audio/Oscillator.h"
#include "util/MidiMath.h"
#include "util/Scale.h"

#include <iostream>

namespace glow {

void MidiMapper::configure(const MidiConfig& midi, const JoystickConfig& joystick) {
  midi_ = midi;
  joystickCfg_ = joystick;
}

void MidiMapper::setRegistry(ParameterRegistry* registry) {
  registry_ = registry;
}

void MidiMapper::setJoystick(Joystick* joystick) {
  joystick_ = joystick;
}

void MidiMapper::handleMessage(const MidiMessage& msg) {
  if (midi_.channel >= 1 && midi_.channel <= 16) {
    const int want = midi_.channel - 1;
    if (static_cast<int>(msg.channel) != want) return;
  }

  switch (msg.type) {
    case MidiMessageType::NoteOn: {
      if (!midi_.noteOn) return;
      NoteEvent ev;
      if (msg.data2 == 0) {
        if (!midi_.noteOff) return;
        ev.type = NoteEvent::Type::NoteOff;
        ev.note = msg.data1;
        ev.velocity = 0;
      } else {
        ev.type = NoteEvent::Type::NoteOn;
        ev.note = msg.data1;
        ev.velocity = midi_.velocity ? msg.data2 : 100;
      }
      if (verbose_) {
        std::cout << "[midi] note " << (ev.type == NoteEvent::Type::NoteOn ? "on" : "off")
                  << " " << static_cast<int>(ev.note)
                  << " vel=" << static_cast<int>(ev.velocity) << "\n";
      }
      if (ev.type == NoteEvent::Type::NoteOn && performance_) {
        performance_->setRoot(static_cast<int>(ev.note));
        soundingNote_ = static_cast<int>(ev.note);
      }
      if (ev.type == NoteEvent::Type::NoteOff && soundingNote_ == static_cast<int>(ev.note)) {
        soundingNote_ = -1;
      }
      if (noteQueue_ && !noteQueue_->push(ev)) {
      }
      break;
    }
    case MidiMessageType::NoteOff: {
      if (!midi_.noteOff) return;
      NoteEvent ev;
      ev.type = NoteEvent::Type::NoteOff;
      ev.note = msg.data1;
      ev.velocity = msg.data2;
      if (verbose_) {
        std::cout << "[midi] note off " << static_cast<int>(ev.note) << "\n";
      }
      if (soundingNote_ == static_cast<int>(ev.note)) soundingNote_ = -1;
      if (noteQueue_) noteQueue_->push(ev);
      break;
    }
    case MidiMessageType::ControlChange:
      handleCc(msg.data1, msg.data2);
      break;
  }
}

void MidiMapper::cycleWaveform() {
  if (registry_ == nullptr) return;
  Waveform current = waveformFromParam(registry_->get(ParameterID::Waveform).target());
  int next = (static_cast<int>(current) + 1) % kWaveformCount;
  const Waveform w = static_cast<Waveform>(next);
  registry_->setTarget(ParameterID::Waveform, waveformToParam(w));
  // Snap immediately so the next audio block hears it
  registry_->get(ParameterID::Waveform).tickSmoothed();
  if (verbose_) {
    std::cout << "[btn] waveform → " << waveformName(w) << "\n";
  }
}

void MidiMapper::cycleScale() {
  if (performance_ == nullptr) return;
  const ScaleId next = nextScale(performance_->scale());
  performance_->setScale(next);
  if (verbose_) {
    std::cout << "[btn] scale → " << scaleName(next) << "\n";
  }
}

void MidiMapper::setPitchFromFifths(int fifths) {
  if (performance_ == nullptr) return;
  int note = performance_->baseRoot() + fifths * 7;
  while (note > 84) note -= 12;
  while (note < 36) note += 12;
  if (note < 0) note = 0;
  if (note > 127) note = 127;

  if (noteQueue_) {
    if (soundingNote_ >= 0) {
      noteQueue_->push({NoteEvent::Type::NoteOff, static_cast<uint8_t>(soundingNote_), 0});
    }
    noteQueue_->push({NoteEvent::Type::NoteOn, static_cast<uint8_t>(note), 100});
  }
  soundingNote_ = note;
  performance_->setRoot(note);
  if (verbose_) {
    std::cout << "[btn] pitch set → MIDI " << note << " (+" << fifths << " fifths from base "
              << performance_->baseRoot() << ")\n";
  }
}

void MidiMapper::handleButtonAction(const ButtonMapping& btn) {
  if (btn.action == "waveform.next") {
    cycleWaveform();
  } else if (btn.action == "scale.next") {
    cycleScale();
  } else if (btn.action == "pitch.set") {
    setPitchFromFifths(btn.fifths);
  } else if (verbose_) {
    std::cout << "[btn] unknown action '" << btn.action << "'\n";
  }
}

void MidiMapper::handleCc(uint8_t cc, uint8_t value) {
  const int ccKey = static_cast<int>(cc);
  const bool pressed = value >= 64;

  if (joystick_ && ccKey == joystickCfg_.buttonCC) {
    if (verbose_ && pressed) {
      std::cout << "[midi] CC" << ccKey << "=" << static_cast<int>(value)
                << " → joystick.button (bank switch)\n";
    }
    joystick_->onButton(pressed);
    return;
  }

  const auto btnIt = midi_.buttons.find(ccKey);
  if (btnIt != midi_.buttons.end()) {
    bool& down = buttonDown_[ccKey];
    if (pressed && !down) {
      handleButtonAction(btnIt->second);
    }
    down = pressed;
    return;
  }

  const auto it = midi_.cc.find(ccKey);
  if (it == midi_.cc.end()) {
    if (verbose_) {
      std::cout << "[midi] CC" << ccKey << "=" << static_cast<int>(value) << " (unmapped)\n";
    }
    return;
  }

  const CcMapping& map = it->second;
  float n = midi_math::ccToNormalized(value);
  n = midi_math::applyCurve(n, map.curve.c_str());

  if (map.parameter == "joystick.x") {
    if (joystick_) joystick_->setRawX(n * 2.0f - 1.0f);
    return;
  }
  if (map.parameter == "joystick.y") {
    if (joystick_) joystick_->setRawY(n * 2.0f - 1.0f);
    return;
  }

  if (registry_ == nullptr) return;
  const auto id = parameterIdFromName(map.parameter);
  if (id == ParameterID::Count) {
    return;
  }
  const float mapped = midi_math::mapNormalized(n, map.min, map.max);
  registry_->setTarget(id, mapped);
  if (verbose_) {
    std::cout << "[midi] CC" << ccKey << "=" << static_cast<int>(value) << " → " << map.parameter
              << " = " << mapped << "\n";
  }
}

void MidiMapper::updateControls() {
  if (joystick_) joystick_->update();
}

}  // namespace glow
