#pragma once

#include "audio/Synth.h"
#include "config/ConfigLoader.h"
#include "control/PerformanceState.h"
#include "parameters/ParameterRegistry.h"
#include "util/LockFreeQueue.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace glow {

class AudioEngine {
 public:
  AudioEngine();
  ~AudioEngine();

  AudioEngine(const AudioEngine&) = delete;
  AudioEngine& operator=(const AudioEngine&) = delete;

  bool start(const AudioConfig& config, ParameterRegistry& registry,
             LockFreeQueue<NoteEvent, 256>& noteQueue,
             PerformanceState* performance = nullptr);
  void stop();

  /** Playback device names. Empty `AudioConfig::device` keeps the system default. */
  static std::vector<std::string> listPlaybackDevices();

  bool isRunning() const { return running_.load(); }
  double sampleRate() const { return sampleRate_; }
  int bufferSize() const { return bufferSize_; }
  const std::string& deviceName() const { return deviceName_; }

  Synth& synth() { return synth_; }

  /** Offline/demo: render numFrames into interleaved stereo without device. */
  void processBlock(float* interleavedStereo, int numFrames);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  Synth synth_;
  std::atomic<bool> running_{false};
  double sampleRate_{48000.0};
  int bufferSize_{128};
  std::string deviceName_;
};

}  // namespace glow
