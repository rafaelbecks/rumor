#pragma once

#include "control/PerformanceState.h"
#include "parameters/Parameter.h"
#include "util/MidiMath.h"

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace glow {

/** Default serial window: 10-bit Arduino analogRead. */
inline constexpr int kSerialInputLow = 0;
inline constexpr int kSerialInputHigh = 1023;

/** Default pitch span for serial mode: MIDI 36 (C2) through 96 (C7). */
inline constexpr float kSerialPitchLow = 36.0f;
inline constexpr float kSerialPitchHigh = 96.0f;

inline constexpr int kSerialBaudDefault = 115200;

/** Max simultaneous serial routes (presets with ≤5 remain valid). */
inline constexpr int kSerialMaxDestinations = 16;

/**
 * Serial destinations. New entries appended — existing preset JSON names stay valid.
 * Pitch is PerformanceState; Waveform is continuous morph 0..3; others map to ParameterID.
 */
enum class SerialDestination {
  Pitch = 0,
  ChordSpread,
  FilterCutoff,
  GrainMix,
  Amplitude,
  FilterResonance,
  FolderDrive,
  FolderMix,
  GrainDensity,
  GrainPitch,
  Attack,
  Release,
  Waveform,
  SineTimbre,
  MidiCc,  // host MIDI CC out (Ableton / other devices); uses SerialRoute::midiCc
  // PaulXStretch engine (names px.*)
  PxStretch,
  PxPitchShift,
  PxFreqShift,
  PxMainVolume,
  PxFftSize,
  PxSpread,
  Count
};

inline const char* serialDestinationName(SerialDestination destination) {
  switch (destination) {
    case SerialDestination::Pitch: return "pitch";
    case SerialDestination::ChordSpread: return "chord.spread";
    case SerialDestination::FilterCutoff: return "filter.cutoff";
    case SerialDestination::GrainMix: return "granular.mix";
    case SerialDestination::Amplitude: return "amplitude";
    case SerialDestination::FilterResonance: return "filter.resonance";
    case SerialDestination::FolderDrive: return "folder.drive";
    case SerialDestination::FolderMix: return "folder.mix";
    case SerialDestination::GrainDensity: return "granular.density";
    case SerialDestination::GrainPitch: return "granular.pitch";
    case SerialDestination::Attack: return "env.attack";
    case SerialDestination::Release: return "env.release";
    case SerialDestination::Waveform: return "osc.waveform";
    case SerialDestination::SineTimbre: return "osc.timbre";
    case SerialDestination::MidiCc: return "midi.cc";
    case SerialDestination::PxStretch: return "px.stretch";
    case SerialDestination::PxPitchShift: return "px.pitch_shift";
    case SerialDestination::PxFreqShift: return "px.freq_shift";
    case SerialDestination::PxMainVolume: return "px.main_volume";
    case SerialDestination::PxFftSize: return "px.fft_size";
    case SerialDestination::PxSpread: return "px.spread";
    case SerialDestination::Count: break;
  }
  return "pitch";
}

inline bool serialDestinationFromName(std::string_view name, SerialDestination& out) {
  if (name == "pitch") {
    out = SerialDestination::Pitch;
    return true;
  }
  if (name == "chord" || name == "chord.spread" || name == "spread") {
    out = SerialDestination::ChordSpread;
    return true;
  }
  if (name == "filter" || name == "filter.cutoff" || name == "cutoff") {
    out = SerialDestination::FilterCutoff;
    return true;
  }
  if (name == "filter.resonance" || name == "resonance") {
    out = SerialDestination::FilterResonance;
    return true;
  }
  if (name == "granular" || name == "granular.mix") {
    out = SerialDestination::GrainMix;
    return true;
  }
  if (name == "granular.density" || name == "density") {
    out = SerialDestination::GrainDensity;
    return true;
  }
  if (name == "granular.pitch") {
    out = SerialDestination::GrainPitch;
    return true;
  }
  if (name == "amplitude" || name == "volume" || name == "amp" || name == "gain" ||
      name == "output") {
    out = SerialDestination::Amplitude;
    return true;
  }
  if (name == "folder.drive" || name == "folder" || name == "drive") {
    out = SerialDestination::FolderDrive;
    return true;
  }
  if (name == "folder.mix") {
    out = SerialDestination::FolderMix;
    return true;
  }
  if (name == "env.attack" || name == "attack") {
    out = SerialDestination::Attack;
    return true;
  }
  if (name == "env.release" || name == "release") {
    out = SerialDestination::Release;
    return true;
  }
  if (name == "osc.waveform" || name == "waveform" || name == "wave") {
    out = SerialDestination::Waveform;
    return true;
  }
  if (name == "osc.timbre" || name == "timbre" || name == "sine.timbre") {
    out = SerialDestination::SineTimbre;
    return true;
  }
  if (name == "midi.cc" || name == "cc" || name == "midi" || name.rfind("cc.", 0) == 0 ||
      name.rfind("midi.cc.", 0) == 0) {
    out = SerialDestination::MidiCc;
    return true;
  }
  if (name == "px.stretch" || name == "stretch") {
    out = SerialDestination::PxStretch;
    return true;
  }
  if (name == "px.pitch_shift" || name == "px.pitch") {
    out = SerialDestination::PxPitchShift;
    return true;
  }
  if (name == "px.freq_shift" || name == "px.freq") {
    out = SerialDestination::PxFreqShift;
    return true;
  }
  if (name == "px.main_volume" || name == "px.volume") {
    out = SerialDestination::PxMainVolume;
    return true;
  }
  if (name == "px.fft_size" || name == "px.fft") {
    out = SerialDestination::PxFftSize;
    return true;
  }
  if (name == "px.spread") {
    out = SerialDestination::PxSpread;
    return true;
  }
  // Avoid bare "mix" → granular (ambiguous with folder.mix); keep old alias for compat.
  if (name == "mix") {
    out = SerialDestination::GrainMix;
    return true;
  }
  return false;
}

/** Parse CC number from "cc.74" / "midi.cc.74". Returns -1 if not present. */
inline int serialCcNumberFromName(std::string_view name) {
  auto parseAfter = [](std::string_view s, std::string_view prefix) -> int {
    if (s.rfind(prefix, 0) != 0) return -1;
    s.remove_prefix(prefix.size());
    if (s.empty()) return -1;
    int v = 0;
    for (char c : s) {
      if (c < '0' || c > '9') return -1;
      v = v * 10 + (c - '0');
      if (v > 127) return -1;
    }
    return v;
  };
  int n = parseAfter(name, "midi.cc.");
  if (n >= 0) return n;
  return parseAfter(name, "cc.");
}

/** Optional: ParameterID for destinations that write the registry (not Pitch). */
inline bool serialDestinationToParameter(SerialDestination destination, ParameterID& out) {
  switch (destination) {
    case SerialDestination::ChordSpread: out = ParameterID::ChordSpread; return true;
    case SerialDestination::FilterCutoff: out = ParameterID::FilterCutoff; return true;
    case SerialDestination::FilterResonance: out = ParameterID::FilterResonance; return true;
    case SerialDestination::GrainMix: out = ParameterID::GrainMix; return true;
    case SerialDestination::GrainDensity: out = ParameterID::GrainDensity; return true;
    case SerialDestination::GrainPitch: out = ParameterID::GrainPitch; return true;
    case SerialDestination::Amplitude: out = ParameterID::Amplitude; return true;
    case SerialDestination::FolderDrive: out = ParameterID::FolderDrive; return true;
    case SerialDestination::FolderMix: out = ParameterID::FolderMix; return true;
    case SerialDestination::Attack: out = ParameterID::Attack; return true;
    case SerialDestination::Release: out = ParameterID::Release; return true;
    case SerialDestination::Waveform: out = ParameterID::Waveform; return true;
    case SerialDestination::SineTimbre: out = ParameterID::SineTimbre; return true;
    case SerialDestination::Pitch:
    case SerialDestination::MidiCc:
    case SerialDestination::PxStretch:
    case SerialDestination::PxPitchShift:
    case SerialDestination::PxFreqShift:
    case SerialDestination::PxMainVolume:
    case SerialDestination::PxFftSize:
    case SerialDestination::PxSpread:
    case SerialDestination::Count:
      break;
  }
  return false;
}

inline bool serialDestinationIsPaulXStretch(SerialDestination destination) {
  switch (destination) {
    case SerialDestination::PxStretch:
    case SerialDestination::PxPitchShift:
    case SerialDestination::PxFreqShift:
    case SerialDestination::PxMainVolume:
    case SerialDestination::PxFftSize:
    case SerialDestination::PxSpread:
      return true;
    default:
      return false;
  }
}

/** One serial reading mapped onto a destination. Up to kSerialMaxDestinations. */
struct SerialRoute {
  SerialDestination destination{SerialDestination::ChordSpread};
  float min{0.0f};
  float max{1.0f};
  bool invert{false};  // swap min/max mapping (high input → min)
  int midiCc{1};       // 0–127 when destination == MidiCc
};

/**
 * Shared serial source plus the same run settings as the command line.
 * Empty optionals mean "not set here"; a command-line flag still wins.
 * droneNote < 0 means the file does not set a drone.
 */
struct SerialConfig {
  std::string port;
  int baud{kSerialBaudDefault};
  bool debug{false};
  int inputLow{kSerialInputLow};
  int inputHigh{kSerialInputHigh};
  int droneNote{-1};
  std::optional<std::string> waveform;
  std::optional<std::string> quantize;
  std::optional<std::string> audio;
  std::optional<bool> verbose;
  std::optional<bool> demo;
  std::optional<double> demoSeconds;
  std::vector<SerialRoute> destinations;
};

/** Legal output span for each destination. */
inline void serialDestinationLimits(SerialDestination destination, float& minimum, float& maximum) {
  switch (destination) {
    case SerialDestination::ChordSpread:
    case SerialDestination::GrainMix:
    case SerialDestination::Amplitude:
    case SerialDestination::FolderDrive:
    case SerialDestination::FolderMix:
    case SerialDestination::SineTimbre:
      minimum = 0.0f;
      maximum = 1.0f;
      return;
    case SerialDestination::FilterResonance:
      minimum = 0.0f;
      maximum = 1.0f;
      return;
    case SerialDestination::FilterCutoff:
      minimum = 40.0f;
      maximum = 16000.0f;
      return;
    case SerialDestination::GrainDensity:
      minimum = 1.0f;
      maximum = 100.0f;
      return;
    case SerialDestination::GrainPitch:
      minimum = -12.0f;
      maximum = 12.0f;
      return;
    case SerialDestination::Attack:
      minimum = 0.001f;
      maximum = 2.0f;
      return;
    case SerialDestination::Release:
      minimum = 0.001f;
      maximum = 4.0f;
      return;
    case SerialDestination::Waveform:
      // Wave indices: 0=sine, 1=saw, 2=square, 3=triangle (fractional = morph).
      minimum = 0.0f;
      maximum = 3.0f;
      return;
    case SerialDestination::Pitch:
    case SerialDestination::MidiCc:
      minimum = 0.0f;
      maximum = 127.0f;
      return;
    case SerialDestination::PxStretch:
      minimum = 0.1f;
      maximum = 1024.0f;
      return;
    case SerialDestination::PxPitchShift:
      minimum = -2400.0f;
      maximum = 2400.0f;
      return;
    case SerialDestination::PxFreqShift:
      minimum = -1000.0f;
      maximum = 1000.0f;
      return;
    case SerialDestination::PxMainVolume:
      minimum = -24.0f;
      maximum = 12.0f;
      return;
    case SerialDestination::PxFftSize:
    case SerialDestination::PxSpread:
      minimum = 0.0f;
      maximum = 1.0f;
      return;
    case SerialDestination::Count:
      break;
  }
  minimum = 0.0f;
  maximum = 1.0f;
}

inline void serialDestinationDefaultRange(SerialDestination destination, float& low, float& high) {
  switch (destination) {
    case SerialDestination::Pitch:
      low = kSerialPitchLow;
      high = kSerialPitchHigh;
      return;
    case SerialDestination::FilterCutoff:
      low = 60.0f;
      high = 12000.0f;
      return;
    case SerialDestination::GrainDensity:
      low = 1.0f;
      high = 40.0f;
      return;
    case SerialDestination::GrainPitch:
      low = -12.0f;
      high = 12.0f;
      return;
    case SerialDestination::Attack:
      low = 0.001f;
      high = 0.5f;
      return;
    case SerialDestination::Release:
      low = 0.05f;
      high = 1.0f;
      return;
    case SerialDestination::Waveform:
      low = 0.0f;
      high = 3.0f;
      return;
    case SerialDestination::MidiCc:
      low = 0.0f;
      high = 127.0f;
      return;
    case SerialDestination::PxStretch:
      low = 0.5f;
      high = 32.0f;
      return;
    case SerialDestination::PxPitchShift:
      low = -700.0f;
      high = 700.0f;
      return;
    case SerialDestination::PxFreqShift:
      low = -200.0f;
      high = 200.0f;
      return;
    case SerialDestination::PxMainVolume:
      low = -12.0f;
      high = 0.0f;
      return;
    case SerialDestination::PxFftSize:
      low = 0.3f;
      high = 0.85f;
      return;
    case SerialDestination::PxSpread:
      low = 0.0f;
      high = 0.6f;
      return;
    case SerialDestination::ChordSpread:
    case SerialDestination::GrainMix:
    case SerialDestination::Amplitude:
    case SerialDestination::FilterResonance:
    case SerialDestination::FolderDrive:
    case SerialDestination::FolderMix:
    case SerialDestination::SineTimbre:
      low = 0.0f;
      high = 1.0f;
      return;
    case SerialDestination::Count:
      break;
  }
  low = 0.0f;
  high = 1.0f;
}

/**
 * Map a raw serial reading from [inLow, inHigh] onto [outLow, outHigh].
 * Readings outside the input window are clamped to the nearest end.
 */
inline float serialMap(float raw, float inLow, float inHigh, float outLow, float outHigh) {
  const float span = inHigh - inLow;
  float t = 0.0f;
  if (span != 0.0f) t = (raw - inLow) / span;
  t = midi_math::clamp(t, 0.0f, 1.0f);
  return outLow + t * (outHigh - outLow);
}

/** Map a 0–1023 reading onto a continuous MIDI note in [low, high]. */
inline float serialAdcToMidi(float adc, float low = kSerialPitchLow,
                             float high = kSerialPitchHigh) {
  return serialMap(adc, static_cast<float>(kSerialInputLow), static_cast<float>(kSerialInputHigh),
                   low, high);
}

/**
 * Pull integers out of a serial byte stream.
 * Values are delimited by any non-digit. Negatives and huge numbers are dropped;
 * the input window is applied later, so readings above 1023 are kept.
 */
class SerialAdcParser {
 public:
  bool feed(char c, int& value) {
    if (skip_) {
      if (c >= '0' && c <= '9') return false;
      skip_ = false;
      return false;
    }
    if (c == '-' && !inNumber_) {
      skip_ = true;
      return false;
    }
    if (c >= '0' && c <= '9') {
      if (!inNumber_) {
        inNumber_ = true;
        acc_ = 0;
        overflow_ = false;
      }
      const int digit = c - '0';
      if (acc_ > (1000000 - digit) / 10) {
        overflow_ = true;
      } else {
        acc_ = acc_ * 10 + digit;
      }
      return false;
    }
    if (!inNumber_) return false;
    inNumber_ = false;
    if (overflow_ || acc_ < 0) return false;
    value = acc_;
    return true;
  }

 private:
  int acc_{0};
  bool inNumber_{false};
  bool overflow_{false};
  bool skip_{false};
};

/** USB serial ports that look like an Arduino / CDC device. */
std::vector<std::string> listSerialPorts();

class ParameterRegistry;

/**
 * Reads newline-delimited integers and maps each one onto destinations
 * (pitch, parameters, waveform morph, …).
 */
class SerialPitch {
 public:
  SerialPitch() = default;
  ~SerialPitch();

  SerialPitch(const SerialPitch&) = delete;
  SerialPitch& operator=(const SerialPitch&) = delete;

  void setPerformance(PerformanceState* performance) { performance_ = performance; }
  void setRegistry(ParameterRegistry* registry) { registry_ = registry; }
  void setVerbose(bool verbose) { verbose_ = verbose; }
  void setDebug(bool debug) { debug_ = debug; }

  void setInputRange(float low, float high) {
    inputLow_ = low;
    inputHigh_ = high;
  }

  void setRoutes(std::vector<SerialRoute> routes) { routes_ = std::move(routes); }

  void setOnReading(std::function<void(int rawAdc, float smoothed)> cb) {
    onReading_ = std::move(cb);
  }

  /** Optional: called from the serial reader thread for midi.cc destinations. */
  void setOnMidiCc(std::function<void(int cc, float value)> cb) { onMidiCc_ = std::move(cb); }
  /** PaulXStretch (and future engines): destination name → mapped native value. */
  void setOnEngineParam(std::function<void(std::string_view id, float value)> cb) {
    onEngineParam_ = std::move(cb);
  }

  int lastRawAdc() const { return lastRawAdc_.load(std::memory_order_relaxed); }
  float lastSmoothedAdc() const { return lastSmoothedAdc_.load(std::memory_order_relaxed); }
  uint64_t readingSequence() const { return readingSeq_.load(std::memory_order_relaxed); }

  float inputLow() const { return inputLow_; }
  float inputHigh() const { return inputHigh_; }
  const std::vector<SerialRoute>& routes() const { return routes_; }

  bool open(const std::string& path, int baud = kSerialBaudDefault);
  void close();

#if defined(_WIN32)
  bool isOpen() const { return handle_ != INVALID_HANDLE_VALUE; }
#else
  bool isOpen() const { return fd_ >= 0; }
#endif
  const std::string& path() const { return path_; }
  int baud() const { return baud_; }

 private:
  void readLoop();
  void processIncomingBytes(const char* buf, int n, class SerialAdcParser& parser,
                            float& smoothed, int& lastLoggedNote);

  PerformanceState* performance_{nullptr};
  ParameterRegistry* registry_{nullptr};
  bool verbose_{false};
  bool debug_{false};
  float inputLow_{static_cast<float>(kSerialInputLow)};
  float inputHigh_{static_cast<float>(kSerialInputHigh)};
  std::vector<SerialRoute> routes_{};
  std::function<void(int, float)> onReading_;
  std::function<void(int, float)> onMidiCc_;
  std::function<void(std::string_view, float)> onEngineParam_;
  std::atomic<int> lastRawAdc_{-1};
  std::atomic<float> lastSmoothedAdc_{-1.0f};
  std::atomic<uint64_t> readingSeq_{0};
#if defined(_WIN32)
  HANDLE handle_{INVALID_HANDLE_VALUE};
#else
  int fd_{-1};
#endif
  int baud_{kSerialBaudDefault};
  std::string path_;
  std::atomic<bool> running_{false};
  std::thread thread_;
};

}  // namespace glow
