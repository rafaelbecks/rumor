#include <catch_amalgamated.hpp>

#include "config/ConfigLoader.h"
#include "control/SerialPitch.h"
#include "util/Scale.h"

namespace {

std::vector<int> parseAll(const std::string& bytes) {
  glow::SerialAdcParser parser;
  std::vector<int> values;
  for (char c : bytes) {
    int value = -1;
    if (parser.feed(c, value)) values.push_back(value);
  }
  return values;
}

}  // namespace

TEST_CASE("Serial ADC maps onto MIDI 36–96", "[serial]") {
  REQUIRE(glow::serialAdcToMidi(0.0f) == Catch::Approx(36.0f));
  REQUIRE(glow::serialAdcToMidi(1023.0f) == Catch::Approx(96.0f));
  REQUIRE(glow::serialAdcToMidi(511.5f) == Catch::Approx(66.0f));
  REQUIRE(glow::serialAdcToMidi(-20.0f) == Catch::Approx(36.0f));
  REQUIRE(glow::serialAdcToMidi(5000.0f) == Catch::Approx(96.0f));
}

TEST_CASE("Serial ADC range can be overridden", "[serial]") {
  REQUIRE(glow::serialAdcToMidi(0.0f, 48.0f, 72.0f) == Catch::Approx(48.0f));
  REQUIRE(glow::serialAdcToMidi(1023.0f, 48.0f, 72.0f) == Catch::Approx(72.0f));
  REQUIRE(glow::serialAdcToMidi(0.0f, 84.0f, 48.0f) == Catch::Approx(84.0f));
  REQUIRE(glow::serialAdcToMidi(1023.0f, 84.0f, 48.0f) == Catch::Approx(48.0f));
}

TEST_CASE("Serial parser accepts delimited 0–1023 values", "[serial]") {
  const auto values = parseAll("0\n512\r\n1023\n");
  REQUIRE(values.size() == 3);
  REQUIRE(values[0] == 0);
  REQUIRE(values[1] == 512);
  REQUIRE(values[2] == 1023);
}

TEST_CASE("Serial parser drops negatives and non-numeric noise", "[serial]") {
  const auto values = parseAll("boot\n-3\n42 ok 7\n");
  REQUIRE(values.size() == 2);
  REQUIRE(values[0] == 42);
  REQUIRE(values[1] == 7);
}

TEST_CASE("Serial parser keeps readings above 1023", "[serial]") {
  const auto values = parseAll("1024\n4095\n");
  REQUIRE(values.size() == 2);
  REQUIRE(values[0] == 1024);
  REQUIRE(values[1] == 4095);
}

TEST_CASE("Serial input window maps onto the destination span", "[serial]") {
  REQUIRE(glow::serialMap(500.0f, 200.0f, 800.0f, 0.0f, 1.0f) == Catch::Approx(0.5f));
  REQUIRE(glow::serialMap(100.0f, 200.0f, 800.0f, 0.0f, 1.0f) == Catch::Approx(0.0f));
  REQUIRE(glow::serialMap(900.0f, 200.0f, 800.0f, 0.0f, 1.0f) == Catch::Approx(1.0f));
  REQUIRE(glow::serialMap(0.0f, 1023.0f, 0.0f, 0.0f, 1.0f) == Catch::Approx(1.0f));
}

TEST_CASE("Serial destination names", "[serial]") {
  glow::SerialDestination dest = glow::SerialDestination::Pitch;
  REQUIRE(glow::serialDestinationFromName("chord.spread", dest));
  REQUIRE(dest == glow::SerialDestination::ChordSpread);
  REQUIRE(glow::serialDestinationFromName("filter.cutoff", dest));
  REQUIRE(dest == glow::SerialDestination::FilterCutoff);
  REQUIRE(glow::serialDestinationFromName("granular.mix", dest));
  REQUIRE(dest == glow::SerialDestination::GrainMix);
  REQUIRE(glow::serialDestinationFromName("volume", dest));
  REQUIRE(dest == glow::SerialDestination::Amplitude);

  REQUIRE(glow::serialDestinationFromName("folder.drive", dest));
  REQUIRE(dest == glow::SerialDestination::FolderDrive);
  REQUIRE(glow::serialDestinationFromName("osc.waveform", dest));
  REQUIRE(dest == glow::SerialDestination::Waveform);
  REQUIRE(glow::serialDestinationFromName("wave", dest));
  REQUIRE(dest == glow::SerialDestination::Waveform);
  REQUIRE(glow::serialDestinationFromName("midi.cc", dest));
  REQUIRE(dest == glow::SerialDestination::MidiCc);
  REQUIRE(glow::serialDestinationFromName("cc.74", dest));
  REQUIRE(dest == glow::SerialDestination::MidiCc);
  REQUIRE(glow::serialCcNumberFromName("cc.74") == 74);
  REQUIRE(glow::serialCcNumberFromName("midi.cc.11") == 11);

  float low = 0.0f;
  float high = 0.0f;
  glow::serialDestinationDefaultRange(glow::SerialDestination::FilterCutoff, low, high);
  REQUIRE(low == Catch::Approx(60.0f));
  REQUIRE(high == Catch::Approx(12000.0f));
  glow::serialDestinationDefaultRange(glow::SerialDestination::Pitch, low, high);
  REQUIRE(low == Catch::Approx(36.0f));
  REQUIRE(high == Catch::Approx(96.0f));
  glow::serialDestinationDefaultRange(glow::SerialDestination::Waveform, low, high);
  REQUIRE(low == Catch::Approx(0.0f));
  REQUIRE(high == Catch::Approx(3.0f));
}

TEST_CASE("Serial JSON template keeps chord and ignores commented destinations", "[serial]") {
  const auto cfg = glow::ConfigLoader::loadSerial("config/serial.json");
  REQUIRE(cfg.has_value());
  REQUIRE(cfg->port == "/dev/cu.usbmodem11101");
  REQUIRE(cfg->baud == 9600);
  REQUIRE(cfg->debug);
  REQUIRE(cfg->droneNote == 55);
  REQUIRE(cfg->inputLow == 2);
  REQUIRE(cfg->inputHigh == 22);
  REQUIRE(cfg->waveform.has_value());
  REQUIRE(*cfg->waveform == "sine");
  REQUIRE(cfg->quantize.has_value());
  REQUIRE(*cfg->quantize == "pentatonic");
  REQUIRE(cfg->destinations.size() == 2);
  REQUIRE(cfg->destinations[0].destination == glow::SerialDestination::ChordSpread);
  REQUIRE(cfg->destinations[1].destination == glow::SerialDestination::Pitch);
  REQUIRE(cfg->destinations[1].min == Catch::Approx(40.0f));
  REQUIRE(cfg->destinations[1].max == Catch::Approx(96.0f));
}

TEST_CASE("Serial JSON accepts four destinations and run settings", "[serial]") {
  const char* ok = R"({
    "waveform": "triangle",
    "quantize": "minor",
    "audio": "BlackHole",
    "verbose": true,
    "input": [0, 80],
    "destinations": [
      { "parameter": "chord.spread", "min": 0, "max": 1 },
      { "parameter": "granular.mix", "min": 0, "max": 1 },
      { "parameter": "pitch", "min": 36, "max": 96 },
      { "parameter": "filter.cutoff", "min": 60, "max": 12000 }
    ]
  })";
  glow::SerialConfig cfg;
  std::string error;
  REQUIRE(glow::ConfigLoader::parseSerialText(ok, cfg, error));
  REQUIRE(cfg.destinations.size() == 4);
  REQUIRE(cfg.waveform.has_value());
  REQUIRE(*cfg.waveform == "triangle");
  REQUIRE(cfg.quantize.has_value());
  REQUIRE(*cfg.quantize == "minor");
  REQUIRE(cfg.audio.has_value());
  REQUIRE(*cfg.audio == "BlackHole");
  REQUIRE(cfg.verbose.has_value());
  REQUIRE(*cfg.verbose);

  const char* tooMany = R"({
    "destinations": [
      { "parameter": "chord.spread", "min": 0, "max": 1 },
      { "parameter": "pitch", "min": 36, "max": 96 },
      { "parameter": "filter.cutoff", "min": 60, "max": 12000 },
      { "parameter": "granular.mix", "min": 0, "max": 1 },
      { "parameter": "pitch", "min": 48, "max": 72 }
    ]
  })";
  REQUIRE_FALSE(glow::ConfigLoader::parseSerialText(tooMany, cfg, error));
}

TEST_CASE("Serial JSON accepts midi.cc destinations", "[serial]") {
  const char* ok = R"({
    "destinations": [
      { "parameter": "midi.cc", "cc": 74, "min": 0, "max": 127 },
      { "parameter": "cc.11", "min": 10, "max": 100 }
    ]
  })";
  glow::SerialConfig cfg;
  std::string error;
  REQUIRE(glow::ConfigLoader::parseSerialText(ok, cfg, error));
  REQUIRE(cfg.destinations.size() == 2);
  REQUIRE(cfg.destinations[0].destination == glow::SerialDestination::MidiCc);
  REQUIRE(cfg.destinations[0].midiCc == 74);
  REQUIRE(cfg.destinations[1].destination == glow::SerialDestination::MidiCc);
  REQUIRE(cfg.destinations[1].midiCc == 11);
  REQUIRE(cfg.destinations[1].min == Catch::Approx(10.0f));
}

TEST_CASE("Serial pitch quantizes to the active scale", "[serial]") {
  const float between = glow::serialAdcToMidi(511.5f);  // MIDI 66, F#4
  const float major = glow::quantizeMidiToScale(between, 60, glow::ScaleId::Major);
  REQUIRE(major == Catch::Approx(67.0f));  // G, nearest C-major degree
  const float raw = glow::quantizeMidiToScale(between, 60, glow::ScaleId::Off);
  REQUIRE(raw == Catch::Approx(between));
}
