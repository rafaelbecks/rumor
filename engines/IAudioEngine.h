#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rumor {

/** Stable parameter metadata exposed to serial mapping / presets / future remote. */
struct EngineParameterInfo {
  std::string id;          // e.g. "amplitude" or "px.stretch"
  std::string displayName;
  float minimum{0.0f};
  float maximum{1.0f};
  float defaultValue{0.0f};
  bool continuous{true};
};

enum class EngineKind {
  Granular,
  PaulXStretch,
};

inline const char* engineKindName(EngineKind kind) {
  switch (kind) {
    case EngineKind::Granular: return "granular";
    case EngineKind::PaulXStretch: return "paulxstretch";
  }
  return "unknown";
}

/**
 * Shared audio-engine interface for RUMOR.
 * Implementations must keep prepare/process/release free of disk I/O and
 * unbounded allocations on the audio thread.
 */
class IAudioEngine {
 public:
  virtual ~IAudioEngine() = default;

  virtual EngineKind kind() const = 0;
  virtual const char* name() const = 0;

  virtual void prepare(double sampleRate, int maxBlockSize) = 0;
  virtual void process(float* left, float* right, int numFrames) = 0;
  virtual void release() = 0;
  virtual void reset() {}

  /** Off-audio-thread: load / clear source material (PaulXStretch). No-op for synth. */
  virtual bool loadSourceFile(const std::string& /*path*/, std::string& errorOut) {
    errorOut = "Source audio not supported by this engine.";
    return false;
  }
  virtual void clearSource() {}
  virtual std::string sourceFilePath() const { return {}; }
  virtual bool hasSource() const { return false; }

  virtual std::vector<EngineParameterInfo> parameters() const = 0;
  /** Set native-range parameter. Thread-safe for RT updates where engine allows. */
  virtual bool setParameter(std::string_view id, float value) = 0;
  virtual bool getParameter(std::string_view id, float& valueOut) const = 0;

  /** Normalized 0–1 helpers (maps via parameters() ranges). */
  bool setParameterNormalized(std::string_view id, float normalized01) {
    for (const auto& p : parameters()) {
      if (p.id != id) continue;
      const float v = p.minimum + (p.maximum - p.minimum) * normalized01;
      return setParameter(id, v);
    }
    return false;
  }
};

}  // namespace rumor
