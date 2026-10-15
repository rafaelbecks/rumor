#include "audio/AudioEngine.h"
#include "audio/Oscillator.h"
#include "config/ConfigLoader.h"
#include "control/Joystick.h"
#include "control/PerformanceState.h"
#include "control/SerialPitch.h"
#include "midi/MidiInput.h"
#include "midi/MidiMapper.h"
#include "parameters/ParameterRegistry.h"
#include "util/LockFreeQueue.h"
#include "util/Scale.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
std::atomic<bool> g_running{true};

void onSignal(int) { g_running.store(false); }

void printUsage(const char* argv0) {
  std::cout
      << "Usage: " << argv0 << " [options]\n"
      << "  --config <path>     mapping JSON (default: config/mapping.json)\n"
      << "  --list-midi         list MIDI input devices and exit\n"
      << "  --list-audio        list audio playback devices and exit\n"
      << "  --audio <name>      playback device (substring, e.g. BlackHole)\n"
      << "                      default: system output, or audio.device in the mapping\n"
      << "  --drone [note]      hold a sustained note (default MIDI 60 / C4)\n"
      << "                      useful when only CC hardware is connected\n"
      << "  --waveform <name>   initial oscillator: sine|saw|square|triangle\n"
      << "                      aliases: sin, sqr, tri (default: saw)\n"
      << "  --quantize [scale]  snap pitch stick to a scale (default: chromatic)\n"
      << "                      scales: major, minor, pentatonic, mixolydian,\n"
      << "                      dorian, phrygian, lydian, locrian, blues,\n"
      << "                      harmonic-minor, melodic-minor, minor-pentatonic,\n"
      << "                      whole-tone, chromatic\n"
      << "  --serial [port]     map one serial integer onto up to 16 destinations\n"
      << "                      port optional (first USB serial device, or the JSON port)\n"
      << "  --serial-config <path>\n"
      << "                      serial JSON (default template: config/serial.json)\n"
      << "                      // comments allowed; overrides a serial section in --config\n"
      << "  --serial-dest <name>\n"
      << "                      one destination: pitch | chord | filter | granular | volume\n"
      << "                      aliases: chord.spread, filter.cutoff, granular.mix, amplitude\n"
      << "                      several destinations belong in the JSON file\n"
      << "  --serial-input <lo> <hi>\n"
      << "                      serial counts at the two ends (default 0 1023)\n"
      << "  --serial-range <lo> <hi>\n"
      << "                      output span for --serial-dest\n"
      << "                      pitch: MIDI notes (default 36 96, legal 0-127)\n"
      << "                      chord / granular / volume: 0-1\n"
      << "                      filter: Hz (default 60 12000, legal 40-16000)\n"
      << "  --serial-debug      print each serial reading and the mapped values\n"
      << "  --serial-baud <n>   serial baud (9600, 19200, 38400, 57600, 115200)\n"
      << "  --verbose, -v       log MIDI / parameter changes to the console\n"
      << "  --demo              play a test tone without MIDI (5 seconds)\n"
      << "  --demo-seconds <n>  demo duration (default 5)\n"
      << "  --help              show this help\n";
}

bool parseMidiNote(const char* s, int& out) {
  if (s == nullptr || *s == '\0') return false;
  char* end = nullptr;
  const long v = std::strtol(s, &end, 10);
  if (end == s || *end != '\0' || v < 0 || v > 127) return false;
  out = static_cast<int>(v);
  return true;
}

bool parseFloatArg(const char* s, float& out) {
  if (s == nullptr || *s == '\0') return false;
  char* end = nullptr;
  const float v = std::strtof(s, &end);
  if (end == s || *end != '\0' || !std::isfinite(v)) return false;
  out = v;
  return true;
}

bool parseSerialCount(const char* s, int& out) {
  if (s == nullptr || *s == '\0') return false;
  char* end = nullptr;
  const long v = std::strtol(s, &end, 10);
  if (end == s || *end != '\0' || v < 0 || v > 1000000) return false;
  out = static_cast<int>(v);
  return true;
}

std::string toLower(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}
}  // namespace

int main(int argc, char** argv) {
  std::string configPath = "config/mapping.json";
  bool listMidi = false;
  bool listAudio = false;
  bool audioOverride = false;
  std::string audioDevice;
  bool demo = false;
  bool drone = false;
  bool droneFromCli = false;
  bool waveformFromCli = false;
  bool quantizeFromCli = false;
  bool verboseFromCli = false;
  bool demoFromCli = false;
  bool demoSecondsFromCli = false;
  bool verbose = false;
  bool serialMode = false;
  std::string serialPort;
  std::string serialConfigPath;
  int serialBaud = glow::kSerialBaudDefault;
  bool serialBaudSet = false;
  bool serialDebug = false;
  bool serialDebugSet = false;
  bool serialRangeSet = false;
  bool serialDestSet = false;
  bool serialInputSet = false;
  glow::SerialDestination serialDest = glow::SerialDestination::Pitch;
  int serialInputLow = glow::kSerialInputLow;
  int serialInputHigh = glow::kSerialInputHigh;
  float serialOutLow = glow::kSerialPitchLow;
  float serialOutHigh = glow::kSerialPitchHigh;
  glow::ScaleId quantizeScale = glow::ScaleId::Off;
  int droneNote = 60;
  double demoSeconds = 5.0;
  glow::Waveform initialWave = glow::Waveform::Saw;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--help") == 0) {
      printUsage(argv[0]);
      return 0;
    }
    if (std::strcmp(argv[i], "--list-midi") == 0) {
      listMidi = true;
    } else if (std::strcmp(argv[i], "--list-audio") == 0) {
      listAudio = true;
    } else if (std::strcmp(argv[i], "--audio") == 0 && i + 1 < argc) {
      audioOverride = true;
      audioDevice = argv[++i];
      if (audioDevice.empty()) {
        std::cerr << "Invalid --audio (pass a device name, or use --list-audio)\n";
        return 1;
      }
    } else if (std::strcmp(argv[i], "--demo") == 0) {
      demo = true;
      demoFromCli = true;
    } else if (std::strcmp(argv[i], "--verbose") == 0 || std::strcmp(argv[i], "-v") == 0) {
      verbose = true;
      verboseFromCli = true;
    } else if (std::strcmp(argv[i], "--quantize") == 0) {
      quantizeFromCli = true;
      quantizeScale = glow::ScaleId::Chromatic;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        const std::string name = toLower(argv[i + 1]);
        glow::ScaleId parsed = glow::ScaleId::Off;
        if (!glow::scaleFromName(name, parsed) || parsed == glow::ScaleId::Off) {
          std::cerr << "Invalid --quantize scale '" << name << "'\n";
          printUsage(argv[0]);
          return 1;
        }
        quantizeScale = parsed;
        ++i;
      }
    } else if (std::strcmp(argv[i], "--waveform") == 0 && i + 1 < argc) {
      waveformFromCli = true;
      const std::string name = toLower(argv[++i]);
      if (!glow::waveformFromName(name, initialWave)) {
        std::cerr << "Invalid --waveform '" << name
                  << "' (use sine|saw|square|triangle)\n";
        return 1;
      }
    } else if (std::strcmp(argv[i], "--drone") == 0) {
      drone = true;
      droneFromCli = true;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        if (!parseMidiNote(argv[i + 1], droneNote)) {
          std::cerr << "Invalid --drone note (use 0–127): " << argv[i + 1] << "\n";
          return 1;
        }
        ++i;
      }
    } else if (std::strcmp(argv[i], "--serial") == 0) {
      serialMode = true;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        serialPort = argv[++i];
      }
    } else if (std::strcmp(argv[i], "--serial-config") == 0 && i + 1 < argc) {
      serialConfigPath = argv[++i];
      serialMode = true;
    } else if (std::strcmp(argv[i], "--serial-baud") == 0 && i + 1 < argc) {
      serialBaud = std::atoi(argv[++i]);
      serialBaudSet = true;
      if (serialBaud <= 0) {
        std::cerr << "Invalid --serial-baud\n";
        return 1;
      }
    } else if (std::strcmp(argv[i], "--serial-dest") == 0 && i + 1 < argc) {
      const std::string name = toLower(argv[++i]);
      serialDestSet = true;
      if (!glow::serialDestinationFromName(name, serialDest)) {
        std::cerr << "Invalid --serial-dest '" << name
                  << "' (use pitch, chord.spread, filter.cutoff, granular.mix, amplitude,\n"
                     "  filter.resonance, folder.drive, folder.mix, granular.density,\n"
                     "  granular.pitch, env.attack, env.release, or osc.waveform)\n";
        return 1;
      }
    } else if (std::strcmp(argv[i], "--serial-input") == 0) {
      if (i + 2 >= argc || !parseSerialCount(argv[i + 1], serialInputLow) ||
          !parseSerialCount(argv[i + 2], serialInputHigh) ||
          serialInputLow == serialInputHigh) {
        std::cerr << "Invalid --serial-input (two different integers, each 0-1000000)\n";
        return 1;
      }
      serialInputSet = true;
      i += 2;
    } else if (std::strcmp(argv[i], "--serial-range") == 0) {
      if (i + 2 >= argc || !parseFloatArg(argv[i + 1], serialOutLow) ||
          !parseFloatArg(argv[i + 2], serialOutHigh) || serialOutLow == serialOutHigh) {
        std::cerr << "Invalid --serial-range (two different numbers)\n";
        return 1;
      }
      serialRangeSet = true;
      i += 2;
    } else if (std::strcmp(argv[i], "--serial-debug") == 0) {
      serialDebug = true;
      serialDebugSet = true;
    } else if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
      configPath = argv[++i];
    } else if (std::strcmp(argv[i], "--demo-seconds") == 0 && i + 1 < argc) {
      demoSecondsFromCli = true;
      demoSeconds = std::atof(argv[++i]);
    } else {
      std::cerr << "Unknown argument: " << argv[i] << "\n";
      printUsage(argv[0]);
      return 1;
    }
  }

  if (serialDestSet || (serialRangeSet && serialConfigPath.empty())) {
    if (!serialRangeSet) {
      glow::serialDestinationDefaultRange(serialDest, serialOutLow, serialOutHigh);
    }
    float serialLimitLow = 0.0f;
    float serialLimitHigh = 1.0f;
    glow::serialDestinationLimits(serialDest, serialLimitLow, serialLimitHigh);
    if (serialOutLow < serialLimitLow || serialOutLow > serialLimitHigh ||
        serialOutHigh < serialLimitLow || serialOutHigh > serialLimitHigh ||
        serialOutLow == serialOutHigh) {
      std::cerr << "Invalid --serial-range for " << glow::serialDestinationName(serialDest)
                << " (each value " << serialLimitLow << "-" << serialLimitHigh << ")\n";
      return 1;
    }
  }

  if (listMidi) {
    const auto devices = glow::MidiInput::listDevices();
    std::cout << "MIDI input devices (" << devices.size() << "):\n";
    for (size_t i = 0; i < devices.size(); ++i) {
      std::cout << "  [" << i << "] " << devices[i] << "\n";
    }
    return 0;
  }

  if (listAudio) {
    const auto devices = glow::AudioEngine::listPlaybackDevices();
    std::cout << "Audio playback devices (" << devices.size() << "):\n";
    for (size_t i = 0; i < devices.size(); ++i) {
      std::cout << "  [" << i << "] " << devices[i] << "\n";
    }
    return 0;
  }

  auto configOpt = glow::ConfigLoader::load(configPath);
  if (!configOpt) {
    std::cerr << "Could not load mapping: " << configPath << "\n";
    return 1;
  }
  glow::AppConfig config = std::move(*configOpt);
  if (audioOverride) config.audio.device = audioDevice;

  glow::SerialConfig serial = config.serial;
  if (!serialConfigPath.empty()) {
    auto loaded = glow::ConfigLoader::loadSerial(serialConfigPath);
    if (!loaded) return 1;
    serial = *loaded;
    serialMode = true;
  } else if (!serial.port.empty() || !serial.destinations.empty()) {
    serialMode = true;
  }

  if (serialMode) {
    if (!serialPort.empty()) serial.port = serialPort;
    else serialPort = serial.port;

    if (serialBaudSet) serial.baud = serialBaud;
    else serialBaud = serial.baud;

    if (serialInputSet) {
      serial.inputLow = serialInputLow;
      serial.inputHigh = serialInputHigh;
    }

    if (serialDebugSet) serial.debug = true;
    serialDebug = serial.debug;

    if (serialDestSet) {
      if (!serialRangeSet) {
        glow::serialDestinationDefaultRange(serialDest, serialOutLow, serialOutHigh);
      }
      serial.destinations.clear();
      serial.destinations.push_back({serialDest, serialOutLow, serialOutHigh});
    } else if (serialRangeSet) {
      if (serial.destinations.size() != 1) {
        std::cerr << "--serial-range needs one destination. Set each min/max in the JSON, "
                     "or pass --serial-dest.\n";
        return 1;
      }
      float limitLow = 0.0f;
      float limitHigh = 1.0f;
      glow::serialDestinationLimits(serial.destinations[0].destination, limitLow, limitHigh);
      if (serialOutLow < limitLow || serialOutLow > limitHigh || serialOutHigh < limitLow ||
          serialOutHigh > limitHigh || serialOutLow == serialOutHigh) {
        std::cerr << "Invalid --serial-range for "
                  << glow::serialDestinationName(serial.destinations[0].destination)
                  << " (each value " << limitLow << "-" << limitHigh << ")\n";
        return 1;
      }
      serial.destinations[0].min = serialOutLow;
      serial.destinations[0].max = serialOutHigh;
    } else if (serial.destinations.empty()) {
      float low = glow::kSerialPitchLow;
      float high = glow::kSerialPitchHigh;
      serial.destinations.push_back({glow::SerialDestination::Pitch, low, high});
    }

    if (serial.droneNote >= 0 && !droneFromCli) droneNote = serial.droneNote;
    drone = true;
  }

  if (serial.waveform && !waveformFromCli) {
    if (!glow::waveformFromName(*serial.waveform, initialWave)) {
      std::cerr << "Invalid waveform '" << *serial.waveform
                << "' (use sine|saw|square|triangle)\n";
      return 1;
    }
  }
  if (serial.quantize && !quantizeFromCli) {
    glow::ScaleId parsed = glow::ScaleId::Off;
    if (!glow::scaleFromName(*serial.quantize, parsed)) {
      std::cerr << "Invalid quantize scale '" << *serial.quantize << "'\n";
      return 1;
    }
    quantizeScale = parsed;
  }
  if (serial.audio && !audioOverride) config.audio.device = *serial.audio;
  if (serial.verbose && *serial.verbose && !verboseFromCli) verbose = true;
  if (serial.demo && *serial.demo && !demoFromCli) demo = true;
  if (serial.demoSeconds && !demoSecondsFromCli) demoSeconds = *serial.demoSeconds;

  glow::ParameterRegistry registry;
  registry.setTarget(glow::ParameterID::Waveform, glow::waveformToParam(initialWave));
  registry.get(glow::ParameterID::Waveform).tickSmoothed();

  glow::PerformanceState performance;
  performance.setRoot(droneNote);
  performance.setBaseRoot(droneNote);
  performance.setScale(quantizeScale);

  glow::SerialPitch serialPitch;
  if (serialMode) {
    if (serialPort.empty()) {
      const auto ports = glow::listSerialPorts();
      if (ports.empty()) {
        std::cerr << "No USB serial port found. Pass --serial <port>\n"
                  << "  macOS:        /dev/cu.usbmodem…\n"
                  << "  Raspberry Pi: /dev/ttyACM0 or /dev/ttyUSB0\n";
        return 1;
      }
      if (ports.size() > 1) {
        std::cout << "Serial devices:\n";
        for (const auto& port : ports) std::cout << "  " << port << "\n";
      }
      serialPort = ports[0];
      if (ports.size() > 1) {
        std::cout << "Using " << serialPort << " (pass --serial <port> to choose)\n";
      }
    }
    serial.port = serialPort;
    serialPitch.setPerformance(&performance);
    serialPitch.setRegistry(&registry);
    serialPitch.setVerbose(verbose);
    serialPitch.setDebug(serial.debug);
    serialPitch.setInputRange(static_cast<float>(serial.inputLow),
                              static_cast<float>(serial.inputHigh));
    serialPitch.setRoutes(serial.destinations);
    if (!serialPitch.open(serial.port, serial.baud)) return 1;
  }

  glow::Joystick joystick;
  joystick.configure(config.joystick);
  joystick.setRegistry(&registry);
  joystick.setPerformance(&performance);
  joystick.setVerbose(verbose);

  glow::LockFreeQueue<glow::NoteEvent, 256> noteQueue;
  glow::MidiMapper mapper;
  mapper.configure(config.midi, config.joystick);
  mapper.setRegistry(&registry);
  mapper.setJoystick(&joystick);
  mapper.setPerformance(&performance);
  mapper.setNoteQueue(&noteQueue);
  mapper.setVerbose(verbose);

  glow::AudioEngine engine;
  if (!engine.start(config.audio, registry, noteQueue, &performance)) {
    std::cerr << "Audio engine failed to start\n";
    return 1;
  }

  std::cout << "Audio device: " << engine.deviceName() << "\n"
            << "Sample rate: " << engine.sampleRate() << "\n"
            << "Buffer size: " << engine.bufferSize() << "\n"
            << "Loaded mapping: " << config.mappingPath << "\n"
            << "Synth: monophonic (" << glow::waveformName(initialWave) << ")\n"
            << "Chord: ldrRange (CC43) → chord.spread (scale tones up/down)\n"
            << "Granular: enabled\n"
            << "Granular: enabled\n";
  bool serialDrivesPitch = false;
  if (serialMode) {
    std::cout << "Serial: " << serialPitch.path() << " @ " << serialPitch.baud() << "\n"
              << "Serial input " << serial.inputLow << "-" << serial.inputHigh << "\n";
    for (const auto& route : serial.destinations) {
      if (route.destination == glow::SerialDestination::Pitch) serialDrivesPitch = true;
      std::cout << "  → " << glow::serialDestinationName(route.destination) << " " << route.min
                << "-" << route.max << "\n";
    }
    if (serialDrivesPitch && quantizeScale != glow::ScaleId::Off) {
      std::cout << "Pitch snapped to " << glow::scaleName(quantizeScale) << " (root MIDI "
                << droneNote << ")\n";
    }
    if (serial.debug) std::cout << "Serial debug: on\n";
  }
  const bool serialPitchQuantized = serialDrivesPitch && quantizeScale != glow::ScaleId::Off;
  if (!serialPitchQuantized && quantizeScale != glow::ScaleId::Off) {
    std::cout << "Pitch/chord scale: " << glow::scaleName(quantizeScale)
              << " (root MIDI " << droneNote << ")\n";
  } else if (!serialPitchQuantized) {
    std::cout << "Pitch: continuous · chord tones: major (no --quantize)\n";
  }
  if (verbose) {
    std::cout << "Verbose: on\n";
  }

  std::cout << "Joystick banks (click to cycle):\n";
  for (size_t i = 0; i < config.joystick.banks.size(); ++i) {
    const auto& b = config.joystick.banks[i];
    std::cout << "  [" << i << "] " << b.name
              << "  X→" << b.x.parameter
              << "  Y→" << b.y.parameter << "\n";
  }
  if (!config.joystick.banks.empty()) {
    std::cout << "Active bank: [0] " << config.joystick.banks[0].name << "\n";
  }

  auto startDrone = [&]() {
    noteQueue.push({glow::NoteEvent::Type::NoteOn,
                    static_cast<uint8_t>(droneNote), 100});
    performance.setBaseRoot(droneNote);
    performance.setRoot(droneNote);
    std::cout << "Drone: MIDI note " << droneNote << " (sustained)\n";
  };

  if (demo) {
    std::cout << "MIDI device: (demo mode — no MIDI)\n";
    if (!drone) {
      droneNote = 60;
    }
    startDrone();
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() <
           demoSeconds) {
      mapper.updateControls();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    noteQueue.push({glow::NoteEvent::Type::NoteOff, static_cast<uint8_t>(droneNote), 0});
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    engine.stop();
    return 0;
  }

  if (drone) {
    startDrone();
  }

  glow::MidiInput midi;
  const bool midiOk = midi.open(config.midi.device, [&](const glow::MidiMessage& msg) {
    mapper.handleMessage(msg);
  });

  if (midiOk) {
    std::cout << "MIDI device: " << midi.deviceName() << "\n";
  } else {
    std::cout << "MIDI device: (none — audio still running; use --drone / --demo or fix MIDI)\n";
  }

  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);
  std::cout << "Running. Ctrl+C to stop.\n";

  while (g_running.load()) {
    mapper.updateControls();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  if (drone) {
    noteQueue.push({glow::NoteEvent::Type::NoteOff, static_cast<uint8_t>(droneNote), 0});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  engine.stop();
  midi.close();
  std::cout << "Stopped.\n";
  return 0;
}
