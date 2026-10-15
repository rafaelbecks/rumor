#include "midi/MidiInput.h"

#include <RtMidi.h>

#include <cctype>
#include <iostream>

namespace glow {

struct MidiInput::Impl {
  std::unique_ptr<RtMidiIn> midi;
  Callback callback;
};

MidiInput::MidiInput() : impl_(std::make_unique<Impl>()) {}
MidiInput::~MidiInput() { close(); }

std::vector<std::string> MidiInput::listDevices() {
  std::vector<std::string> names;
  try {
    RtMidiIn midi;
    const unsigned n = midi.getPortCount();
    names.reserve(n);
    for (unsigned i = 0; i < n; ++i) {
      names.push_back(midi.getPortName(i));
    }
  } catch (const RtMidiError& e) {
    std::cerr << "RtMidi error listing ports: " << e.getMessage() << "\n";
  }
  return names;
}

bool MidiInput::open(const std::string& deviceSubstring, Callback cb) {
  close();
  try {
    impl_->midi = std::make_unique<RtMidiIn>();
    impl_->callback = std::move(cb);

    const unsigned n = impl_->midi->getPortCount();
    if (n == 0) {
      std::cerr << "No MIDI input devices found\n";
      return false;
    }

    auto matchScore = [&](const std::string& name) -> int {
      if (deviceSubstring.empty()) return 0;
      if (name == deviceSubstring) return 300;
      // case-insensitive substring
      auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
      };
      const std::string nl = lower(name);
      const std::string ql = lower(deviceSubstring);
      if (nl.find(ql) != std::string::npos) return 200;
      // serial_to_midi-style: "IAC Driver Bus 5" ↔ any port with both "iac" and "5"
      if (ql.find("iac") != std::string::npos && ql.find('5') != std::string::npos) {
        if (nl.find("iac") != std::string::npos &&
            (nl.find("bus 5") != std::string::npos || nl.find("bus5") != std::string::npos ||
             nl.find(" 5") != std::string::npos)) {
          return 150;
        }
      }
      return -1;
    };

    unsigned chosen = 0;
    if (!deviceSubstring.empty()) {
      int best = -1;
      bool found = false;
      for (unsigned i = 0; i < n; ++i) {
        const std::string name = impl_->midi->getPortName(i);
        const int score = matchScore(name);
        if (score > best) {
          best = score;
          chosen = i;
          found = score >= 0;
        }
      }
      if (!found) {
        std::cerr << "MIDI device matching '" << deviceSubstring
                  << "' not found; available:\n";
        for (unsigned i = 0; i < n; ++i) {
          std::cerr << "  [" << i << "] " << impl_->midi->getPortName(i) << "\n";
        }
        return false;
      }
    }

    deviceName_ = impl_->midi->getPortName(chosen);
    impl_->midi->ignoreTypes(true, true, true);
    impl_->midi->setCallback(
        [](double /*dt*/, std::vector<unsigned char>* message, void* user) {
          auto* self = static_cast<MidiInput*>(user);
          if (!self || !message || message->empty() || !self->impl_->callback) return;
          const auto& bytes = *message;
          if (bytes.size() < 2) return;
          const uint8_t status = bytes[0];
          const uint8_t type = status & 0xF0;
          const uint8_t channel = status & 0x0F;
          MidiMessage msg;
          msg.channel = channel;
          if (type == 0x90) {
            msg.type = MidiMessageType::NoteOn;
            msg.data1 = bytes[1];
            msg.data2 = bytes.size() > 2 ? bytes[2] : 0;
          } else if (type == 0x80) {
            msg.type = MidiMessageType::NoteOff;
            msg.data1 = bytes[1];
            msg.data2 = bytes.size() > 2 ? bytes[2] : 0;
          } else if (type == 0xB0) {
            msg.type = MidiMessageType::ControlChange;
            msg.data1 = bytes[1];
            msg.data2 = bytes.size() > 2 ? bytes[2] : 0;
          } else {
            return;
          }
          self->impl_->callback(msg);
        },
        this);

    impl_->midi->openPort(chosen);
    open_ = true;
    return true;
  } catch (const RtMidiError& e) {
    std::cerr << "RtMidi open failed: " << e.getMessage() << "\n";
    return false;
  }
}

void MidiInput::close() {
  if (impl_ && impl_->midi) {
    try {
      impl_->midi->cancelCallback();
      if (impl_->midi->isPortOpen()) impl_->midi->closePort();
    } catch (...) {
    }
    impl_->midi.reset();
  }
  open_ = false;
  deviceName_.clear();
}

}  // namespace glow
