#pragma once

#include "control/SerialPitch.h"
#include "parameters/Parameter.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace glow {

struct CcMapping {
  std::string parameter;  // may be "joystick.x", "joystick.y", or a Parameter name
  float min{0.0f};
  float max{1.0f};
  std::string curve{"linear"};
};

/** Momentary button → discrete action (press edge). */
struct ButtonMapping {
  std::string action;  // waveform.next | scale.next | pitch.set
  int fifths{0};       // for pitch.set: 0 = base root, 1 = +P5, …
  std::string hardware;
};

struct AxisMapping {
  std::string parameter;
  float min{0.0f};
  float max{1.0f};
  std::string curve{"linear"};
};

struct JoystickBank {
  std::string name;
  AxisMapping x;
  AxisMapping y;
};

struct JoystickConfig {
  int buttonCC{3};
  float deadzone{0.08f};
  float smoothing{0.15f};
  bool invertX{false};
  bool invertY{false};
  std::vector<JoystickBank> banks;
};

struct MidiConfig {
  int channel{1};  // 1–16 in JSON; stored as 1–16
  bool noteOn{true};
  bool noteOff{true};
  bool velocity{true};
  std::string device;  // optional substring match; empty = first available
  std::unordered_map<int, CcMapping> cc;
  std::unordered_map<int, ButtonMapping> buttons;
};

struct AudioConfig {
  double sampleRate{48000.0};
  int bufferSize{128};
  int maxGrains{32};
  std::string device;  // playback name substring; empty = system default
};

struct AppConfig {
  MidiConfig midi;
  JoystickConfig joystick;
  AudioConfig audio;
  SerialConfig serial;
  std::string mappingPath;
};

class ConfigLoader {
 public:
  /** Load mapping JSON. Returns nullopt on hard failure. */
  static std::optional<AppConfig> load(const std::string& path);

  /**
   * Load a serial template. The file may be the serial object itself, or a
   * mapping that contains a "serial" object. // comments are allowed.
   */
  static std::optional<SerialConfig> loadSerial(const std::string& path);

  /** Parse serial JSON text (comments allowed). Returns false and sets errorOut. */
  static bool parseSerialText(const std::string& text, SerialConfig& out, std::string& errorOut);

  /** Soft validation helpers used by tests. */
  static bool validateCcTargets(const AppConfig& config, std::string& errorOut);
};

}  // namespace glow
