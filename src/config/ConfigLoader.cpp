#include "config/ConfigLoader.h"

#include "parameters/Parameter.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>

namespace glow {
namespace {

AxisMapping parseAxis(const nlohmann::json& j) {
  AxisMapping a;
  a.parameter = j.value("parameter", "");
  a.min = j.value("min", 0.0f);
  a.max = j.value("max", 1.0f);
  a.curve = j.value("curve", "linear");
  return a;
}

std::string lowerCopy(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

nlohmann::json parseJsonText(const std::string& text) {
  return nlohmann::json::parse(text, nullptr, true, true);
}

bool parseSerialObject(const nlohmann::json& j, SerialConfig& out, std::string& errorOut) {
  out = SerialConfig{};
  if (!j.is_object()) {
    errorOut = "serial config must be a JSON object";
    return false;
  }

  if (j.contains("serial") && j["serial"].is_string()) out.port = j["serial"].get<std::string>();
  if (j.contains("port") && j["port"].is_string()) out.port = j["port"].get<std::string>();
  out.baud = j.value("baud", kSerialBaudDefault);
  out.debug = j.value("debug", false);
  if (j.contains("waveform")) {
    if (!j["waveform"].is_string()) {
      errorOut = "waveform must be sine, saw, square, or triangle";
      return false;
    }
    out.waveform = lowerCopy(j["waveform"].get<std::string>());
  }
  if (j.contains("quantize")) {
    if (j["quantize"].is_boolean()) {
      out.quantize = j["quantize"].get<bool>() ? std::string("chromatic") : std::string("off");
    } else if (j["quantize"].is_string()) {
      out.quantize = lowerCopy(j["quantize"].get<std::string>());
    } else {
      errorOut = "quantize must be a scale name or true/false";
      return false;
    }
  }
  if (j.contains("audio")) {
    if (!j["audio"].is_string()) {
      errorOut = "audio must be a playback device name";
      return false;
    }
    out.audio = j["audio"].get<std::string>();
  }
  if (j.contains("verbose")) {
    if (!j["verbose"].is_boolean()) {
      errorOut = "verbose must be true or false";
      return false;
    }
    out.verbose = j["verbose"].get<bool>();
  }
  if (j.contains("demo")) {
    if (!j["demo"].is_boolean()) {
      errorOut = "demo must be true or false";
      return false;
    }
    out.demo = j["demo"].get<bool>();
  }
  if (j.contains("demoSeconds")) {
    if (!j["demoSeconds"].is_number()) {
      errorOut = "demoSeconds must be a number";
      return false;
    }
    out.demoSeconds = j["demoSeconds"].get<double>();
  }
  if (j.contains("drone")) {
    const int note = j.value("drone", -1);
    if (note < 0 || note > 127) {
      errorOut = "drone must be a MIDI note 0-127";
      return false;
    }
    out.droneNote = note;
  }
  if (j.contains("input")) {
    const auto& input = j["input"];
    if (!input.is_array() || input.size() != 2) {
      errorOut = "input must be [low, high]";
      return false;
    }
    if (!input[0].is_number() || !input[1].is_number()) {
      errorOut = "input must be [low, high]";
      return false;
    }
    out.inputLow = static_cast<int>(input[0].get<double>());
    out.inputHigh = static_cast<int>(input[1].get<double>());
    if (out.inputLow == out.inputHigh || out.inputLow < 0 || out.inputHigh < 0 ||
        out.inputLow > 1000000 || out.inputHigh > 1000000) {
      errorOut = "input ends must be two different integers, each 0-1000000";
      return false;
    }
  }
  if (out.baud <= 0) {
    errorOut = "baud must be positive";
    return false;
  }

  if (j.contains("wsUrl") && j["wsUrl"].is_string()) out.wsUrl = j["wsUrl"].get<std::string>();
  if (j.contains("wsKeys")) {
    const auto& keys = j["wsKeys"];
    if (!keys.is_array()) {
      errorOut = "wsKeys must be an array";
      return false;
    }
    if (keys.size() > 64) {
      errorOut = "at most 64 websocket keys";
      return false;
    }
    for (const auto& item : keys) {
      if (!item.is_object() || !item.contains("key") || !item["key"].is_string()) {
        errorOut = "wsKeys entries need a string \"key\"";
        return false;
      }
      WsInput input;
      input.key = item["key"].get<std::string>();
      if (input.key.empty() || input.key == "serial") {
        errorOut = "websocket key must be a non-empty name other than serial";
        return false;
      }
      for (const auto& existing : out.wsKeys) {
        if (existing.key == input.key) {
          errorOut = "duplicate websocket key " + input.key;
          return false;
        }
      }
      input.low = item.value("low", 0.0f);
      input.high = item.value("high", 1.0f);
      input.enabled = item.value("enabled", false);
      const bool emptySpan = !(input.low < input.high) && !(input.high < input.low);
      if (emptySpan) {
        errorOut = "websocket key " + input.key + " needs two different range ends";
        return false;
      }
      out.wsKeys.push_back(std::move(input));
    }
  }

  if (!j.contains("destinations")) return true;
  const auto& destinations = j["destinations"];
  if (!destinations.is_array()) {
    errorOut = "destinations must be an array";
    return false;
  }
  if (destinations.size() > static_cast<size_t>(kSerialMaxDestinations)) {
    errorOut = "at most " + std::to_string(kSerialMaxDestinations) + " serial destinations";
    return false;
  }

  bool seen[static_cast<int>(SerialDestination::Count)] = {};
  bool seenCc[128] = {};
  for (const auto& item : destinations) {
    const std::string name = lowerCopy(item.value("parameter", item.value("dest", "")));
    SerialDestination destination = SerialDestination::ChordSpread;
    if (!serialDestinationFromName(name, destination)) {
      errorOut = "unknown serial destination '" + name + "'";
      return false;
    }

    int midiCc = 1;
    if (destination == SerialDestination::MidiCc) {
      const int fromName = serialCcNumberFromName(name);
      if (fromName >= 0) {
        midiCc = fromName;
      } else if (item.contains("cc")) {
        if (!item["cc"].is_number_integer() && !item["cc"].is_number()) {
          errorOut = "midi.cc needs an integer \"cc\" (0-127)";
          return false;
        }
        midiCc = item["cc"].get<int>();
      } else {
        errorOut = "midi.cc needs \"cc\": <0-127> (or use parameter \"cc.74\")";
        return false;
      }
      if (midiCc < 0 || midiCc > 127) {
        errorOut = "midi.cc cc must be 0-127";
        return false;
      }
      if (seenCc[midiCc]) {
        errorOut = "duplicate midi.cc for CC " + std::to_string(midiCc);
        return false;
      }
      seenCc[midiCc] = true;
    } else {
      const int index = static_cast<int>(destination);
      if (index >= 0 && index < static_cast<int>(SerialDestination::Count) && seen[index]) {
        errorOut = std::string("duplicate serial destination ") + serialDestinationName(destination);
        return false;
      }
      if (index >= 0 && index < static_cast<int>(SerialDestination::Count)) seen[index] = true;
    }

    float low = 0.0f;
    float high = 1.0f;
    serialDestinationDefaultRange(destination, low, high);
    if (item.contains("min")) {
      if (!item["min"].is_number()) {
        errorOut = std::string(serialDestinationName(destination)) + " min must be a number";
        return false;
      }
      low = item["min"].get<float>();
    }
    if (item.contains("max")) {
      if (!item["max"].is_number()) {
        errorOut = std::string(serialDestinationName(destination)) + " max must be a number";
        return false;
      }
      high = item["max"].get<float>();
    }
    float limitLow = 0.0f;
    float limitHigh = 1.0f;
    serialDestinationLimits(destination, limitLow, limitHigh);
    if (low == high || low < limitLow || low > limitHigh || high < limitLow || high > limitHigh) {
      errorOut = std::string(serialDestinationName(destination)) + " range must be two different values, each " +
                 std::to_string(limitLow) + "-" + std::to_string(limitHigh);
      return false;
    }
    std::string source;
    if (item.contains("source") && item["source"].is_string())
      source = item["source"].get<std::string>();
    if (source == "serial") source.clear();
    out.destinations.push_back(
        SerialRoute{destination, low, high, item.value("invert", false), midiCc, std::move(source)});
  }
  return true;
}

}  // namespace

std::optional<AppConfig> ConfigLoader::load(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    std::cerr << "Failed to open config: " << path << "\n";
    return std::nullopt;
  }

  nlohmann::json root;
  try {
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    root = parseJsonText(text);
  } catch (const std::exception& e) {
    std::cerr << "JSON parse error in " << path << ": " << e.what() << "\n";
    return std::nullopt;
  }

  AppConfig cfg;
  cfg.mappingPath = path;

  if (root.contains("audio")) {
    const auto& a = root["audio"];
    cfg.audio.sampleRate = a.value("sampleRate", 48000.0);
    cfg.audio.bufferSize = a.value("bufferSize", 128);
    cfg.audio.maxGrains = a.value("maxGrains", 32);
    cfg.audio.device = a.value("device", "");
  }

  if (root.contains("midi")) {
    const auto& m = root["midi"];
    cfg.midi.channel = m.value("channel", 1);
    cfg.midi.device = m.value("device", "");
    if (m.contains("notes")) {
      cfg.midi.noteOn = m["notes"].value("noteOn", true);
      cfg.midi.noteOff = m["notes"].value("noteOff", true);
      cfg.midi.velocity = m["notes"].value("velocity", true);
    }
    if (m.contains("cc") && m["cc"].is_object()) {
      for (auto it = m["cc"].begin(); it != m["cc"].end(); ++it) {
        int cc = 0;
        try {
          cc = std::stoi(it.key());
        } catch (...) {
          std::cerr << "Warning: invalid CC key '" << it.key() << "' ignored\n";
          continue;
        }
        CcMapping map;
        map.parameter = it.value().value("parameter", "");
        map.min = it.value().value("min", 0.0f);
        map.max = it.value().value("max", 1.0f);
        map.curve = it.value().value("curve", "linear");
        if (map.parameter.empty()) {
          std::cerr << "Warning: CC " << cc << " missing parameter, ignored\n";
          continue;
        }
        cfg.midi.cc[cc] = map;
      }
    }
    if (m.contains("buttons") && m["buttons"].is_object()) {
      for (auto it = m["buttons"].begin(); it != m["buttons"].end(); ++it) {
        int cc = 0;
        try {
          cc = std::stoi(it.key());
        } catch (...) {
          std::cerr << "Warning: invalid button CC key '" << it.key() << "' ignored\n";
          continue;
        }
        ButtonMapping btn;
        btn.action = it.value().value("action", "");
        btn.fifths = it.value().value("fifths", 0);
        btn.hardware = it.value().value("hardware", "");
        if (btn.action.empty()) {
          std::cerr << "Warning: button CC " << cc << " missing action, ignored\n";
          continue;
        }
        cfg.midi.buttons[cc] = btn;
      }
    }
  }

  if (root.contains("joystick")) {
    const auto& j = root["joystick"];
    cfg.joystick.buttonCC = j.value("buttonCC", 3);
    cfg.joystick.deadzone = j.value("deadzone", 0.08f);
    cfg.joystick.smoothing = j.value("smoothing", 0.15f);
    cfg.joystick.invertX = j.value("invertX", false);
    cfg.joystick.invertY = j.value("invertY", false);
    if (j.contains("banks") && j["banks"].is_array()) {
      for (const auto& b : j["banks"]) {
        JoystickBank bank;
        bank.name = b.value("name", "bank");
        if (b.contains("x")) bank.x = parseAxis(b["x"]);
        if (b.contains("y")) bank.y = parseAxis(b["y"]);
        cfg.joystick.banks.push_back(std::move(bank));
      }
    }
  }

  if (root.contains("serial")) {
    std::string serialError;
    if (!parseSerialObject(root["serial"], cfg.serial, serialError)) {
      std::cerr << "Serial config error in " << path << ": " << serialError << "\n";
      return std::nullopt;
    }
  }

  std::string err;
  if (!validateCcTargets(cfg, err)) {
    std::cerr << "Config warning: " << err << "\n";
  }

  return cfg;
}

std::optional<SerialConfig> ConfigLoader::loadSerial(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    std::cerr << "Failed to open serial config: " << path << "\n";
    return std::nullopt;
  }
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  SerialConfig cfg;
  std::string error;
  if (!parseSerialText(text, cfg, error)) {
    std::cerr << "Serial config error in " << path << ": " << error << "\n";
    return std::nullopt;
  }
  return cfg;
}

bool ConfigLoader::parseSerialText(const std::string& text, SerialConfig& out, std::string& errorOut) {
  try {
    const nlohmann::json root = parseJsonText(text);
    const nlohmann::json& object =
        (root.contains("serial") && root["serial"].is_object()) ? root["serial"] : root;
    return parseSerialObject(object, out, errorOut);
  } catch (const std::exception& e) {
    errorOut = e.what();
    return false;
  }
}

bool ConfigLoader::validateCcTargets(const AppConfig& config, std::string& errorOut) {
  for (const auto& [cc, map] : config.midi.cc) {
    if (map.parameter == "joystick.x" || map.parameter == "joystick.y") {
      continue;
    }
    if (parameterIdFromName(map.parameter) == ParameterID::Count) {
      errorOut = "Unknown parameter '" + map.parameter + "' for CC " + std::to_string(cc);
      return false;
    }
  }
  for (const auto& bank : config.joystick.banks) {
    for (const auto* axis : {&bank.x, &bank.y}) {
      if (axis->parameter.empty()) continue;
      if (parameterIdFromName(axis->parameter) == ParameterID::Count) {
        errorOut = "Unknown bank parameter '" + axis->parameter + "' in bank " + bank.name;
        return false;
      }
    }
  }
  return true;
}

}  // namespace glow
