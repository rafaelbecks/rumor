#include <catch_amalgamated.hpp>

#include "config/ConfigLoader.h"
#include "midi/MidiMapper.h"
#include "parameters/ParameterRegistry.h"
#include "util/LockFreeQueue.h"

#include <cmath>
#include <fstream>

TEST_CASE("JSON configuration loading", "[config]") {
  const auto cfg = glow::ConfigLoader::load("config/mapping.json");
  REQUIRE(cfg.has_value());
  REQUIRE(cfg->midi.channel == 1);
  REQUIRE(cfg->midi.device.find("IAC") != std::string::npos);
  REQUIRE(cfg->midi.device.find("5") != std::string::npos);

  // Must match GLOW serial_to_midi.py DEFAULT_CC + custom-hardware.json
  REQUIRE(cfg->midi.cc.count(21) == 1);  // pot1
  REQUIRE(cfg->midi.cc.count(22) == 1);  // pot2
  REQUIRE(cfg->midi.cc.count(23) == 1);  // pot3
  REQUIRE(cfg->midi.cc.count(43) == 1);  // ldrRange
  REQUIRE(cfg->midi.cc.at(43).parameter == "chord.spread");
  REQUIRE(cfg->midi.cc.at(24).parameter == "joystick.x");  // joyX
  REQUIRE(cfg->midi.cc.at(25).parameter == "joystick.y");  // joyY
  REQUIRE(cfg->joystick.buttonCC == 26);                   // joyBtn
  REQUIRE(cfg->joystick.banks.size() == 2);
  REQUIRE(cfg->joystick.banks[0].x.parameter == "pitch.offset");
  REQUIRE(cfg->joystick.banks[0].x.min == Catch::Approx(-1200.0f));
  REQUIRE(cfg->joystick.banks[0].x.max == Catch::Approx(1200.0f));
  REQUIRE(cfg->joystick.banks[1].name == "granular");
  REQUIRE(cfg->joystick.banks[1].x.parameter == "granular.density");
  REQUIRE(cfg->joystick.banks[1].y.parameter == "granular.pitch");
  REQUIRE(cfg->midi.buttons.count(37) == 1);
  REQUIRE(cfg->midi.buttons.at(37).action == "waveform.next");
  REQUIRE(cfg->midi.buttons.at(38).action == "scale.next");
  REQUIRE(cfg->midi.buttons.at(29).action == "pitch.set");
  REQUIRE(cfg->midi.buttons.at(29).fifths == 0);
  REQUIRE(cfg->midi.buttons.at(33).fifths == 3);

  glow::ParameterRegistry reg;
  REQUIRE(reg.get(glow::ParameterID::Waveform).defaultValue() >= 0.5f);
}

TEST_CASE("Custom hardware joystick CC path", "[config]") {
  auto cfgOpt = glow::ConfigLoader::load("config/mapping.json");
  REQUIRE(cfgOpt.has_value());
  auto& cfg = *cfgOpt;

  glow::ParameterRegistry reg;
  glow::Joystick joy;
  joy.configure(cfg.joystick);
  joy.setRegistry(&reg);

  glow::LockFreeQueue<glow::NoteEvent, 256> notes;
  glow::MidiMapper mapper;
  mapper.configure(cfg.midi, cfg.joystick);
  mapper.setRegistry(&reg);
  mapper.setJoystick(&joy);
  mapper.setNoteQueue(&notes);

  // Center stick (MIDI 64) should sit in deadzone → pitch offset ~0
  glow::MidiMessage joyX;
  joyX.type = glow::MidiMessageType::ControlChange;
  joyX.channel = 0;
  joyX.data1 = 24;
  joyX.data2 = 64;
  mapper.handleMessage(joyX);
  glow::MidiMessage joyY;
  joyY.type = glow::MidiMessageType::ControlChange;
  joyY.channel = 0;
  joyY.data1 = 25;
  joyY.data2 = 64;
  mapper.handleMessage(joyY);
  mapper.updateControls();
  REQUIRE(std::abs(reg.get(glow::ParameterID::PitchOffset).target()) < 1.0f);

  // joyBtn (CC26) press advances bank — same as GLOW nextCC
  REQUIRE(joy.activeBank() == 0);
  glow::MidiMessage btn;
  btn.type = glow::MidiMessageType::ControlChange;
  btn.channel = 0;
  btn.data1 = 26;
  btn.data2 = 127;
  mapper.handleMessage(btn);
  btn.data2 = 0;
  mapper.handleMessage(btn);
  REQUIRE(joy.activeBank() == 1);
}

TEST_CASE("Invalid MIDI mapping handling", "[config]") {
  const char* path = "config/_invalid_test.json";
  {
    std::ofstream out(path);
    out << R"({
      "midi": {
        "channel": 1,
        "cc": {
          "99": { "parameter": "not.a.real.param", "min": 0, "max": 1 }
        }
      },
      "joystick": { "buttonCC": 3, "banks": [] }
    })";
  }

  const auto cfg = glow::ConfigLoader::load(path);
  REQUIRE(cfg.has_value());
  std::string err;
  REQUIRE_FALSE(glow::ConfigLoader::validateCcTargets(*cfg, err));
  REQUIRE(err.find("not.a.real.param") != std::string::npos);
}

TEST_CASE("MIDI mapper note queue and CC", "[config]") {
  auto cfgOpt = glow::ConfigLoader::load("config/mapping.json");
  REQUIRE(cfgOpt.has_value());
  auto& cfg = *cfgOpt;

  glow::ParameterRegistry reg;
  glow::Joystick joy;
  joy.configure(cfg.joystick);
  joy.setRegistry(&reg);

  glow::LockFreeQueue<glow::NoteEvent, 256> notes;
  glow::MidiMapper mapper;
  mapper.configure(cfg.midi, cfg.joystick);
  mapper.setRegistry(&reg);
  mapper.setJoystick(&joy);
  mapper.setNoteQueue(&notes);

  glow::MidiMessage on;
  on.type = glow::MidiMessageType::NoteOn;
  on.channel = 0;
  on.data1 = 60;
  on.data2 = 100;
  mapper.handleMessage(on);

  glow::NoteEvent ev;
  REQUIRE(notes.pop(ev));
  REQUIRE(ev.type == glow::NoteEvent::Type::NoteOn);
  REQUIRE(ev.note == 60);
  REQUIRE(ev.velocity == 100);

  glow::MidiMessage cc;
  cc.type = glow::MidiMessageType::ControlChange;
  cc.channel = 0;
  cc.data1 = 21;
  cc.data2 = 127;
  mapper.handleMessage(cc);
  REQUIRE(reg.get(glow::ParameterID::FilterResonance).target() == Catch::Approx(0.95f));
}
