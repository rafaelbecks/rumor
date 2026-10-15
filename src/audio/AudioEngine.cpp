#include "audio/AudioEngine.h"

#define MA_NO_DECODING
#define MA_NO_ENCODING
#include "miniaudio.h"

#include <cctype>
#include <iostream>
#include <vector>

namespace glow {
namespace {

std::string lowerCopy(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

int deviceMatchScore(const std::string& name, const std::string& query) {
  if (query.empty()) return 0;
  if (name == query) return 300;
  if (lowerCopy(name).find(lowerCopy(query)) != std::string::npos) return 200;
  return -1;
}

}  // namespace

struct AudioEngine::Impl {
  ma_context context{};
  bool contextInitialized{false};
  ma_device device{};
  bool deviceInitialized{false};
  ma_device_id playbackId{};
  std::vector<float> scratchL;
  std::vector<float> scratchR;
  AudioEngine* self{nullptr};
};

AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {
  impl_->self = this;
}

AudioEngine::~AudioEngine() { stop(); }

std::vector<std::string> AudioEngine::listPlaybackDevices() {
  std::vector<std::string> names;
  ma_context context{};
  if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) return names;

  ma_device_info* infos = nullptr;
  ma_uint32 count = 0;
  if (ma_context_get_devices(&context, &infos, &count, nullptr, nullptr) == MA_SUCCESS) {
    names.reserve(count);
    for (ma_uint32 i = 0; i < count; ++i) {
      if (infos[i].name[0] != '\0') names.emplace_back(infos[i].name);
    }
  }
  ma_context_uninit(&context);
  return names;
}

bool AudioEngine::start(const AudioConfig& config, ParameterRegistry& registry,
                        LockFreeQueue<NoteEvent, 256>& noteQueue,
                        PerformanceState* performance) {
  stop();

  sampleRate_ = config.sampleRate;
  bufferSize_ = config.bufferSize;

  synth_.prepare(static_cast<float>(sampleRate_), config.maxGrains);
  synth_.setParameterRegistry(&registry);
  synth_.setPerformance(performance);
  synth_.setNoteQueue(&noteQueue);

  impl_->scratchL.assign(static_cast<size_t>(bufferSize_), 0.0f);
  impl_->scratchR.assign(static_cast<size_t>(bufferSize_), 0.0f);

  if (ma_context_init(nullptr, 0, nullptr, &impl_->context) != MA_SUCCESS) {
    std::cerr << "Failed to initialize audio context\n";
    return false;
  }
  impl_->contextInitialized = true;

  ma_device_info* infos = nullptr;
  ma_uint32 deviceCount = 0;
  if (ma_context_get_devices(&impl_->context, &infos, &deviceCount, nullptr, nullptr) !=
      MA_SUCCESS) {
    std::cerr << "Failed to list audio devices\n";
    stop();
    return false;
  }

  ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
  deviceConfig.playback.format = ma_format_f32;
  deviceConfig.playback.channels = 2;
  deviceConfig.sampleRate = static_cast<ma_uint32>(sampleRate_);
  deviceConfig.periodSizeInFrames = static_cast<ma_uint32>(bufferSize_);
  deviceConfig.pUserData = impl_.get();

  if (!config.device.empty()) {
    int best = -1;
    ma_uint32 chosen = 0;
    for (ma_uint32 i = 0; i < deviceCount; ++i) {
      if (infos[i].name[0] == '\0') continue;
      const int score = deviceMatchScore(infos[i].name, config.device);
      if (score > best) {
        best = score;
        chosen = i;
      }
    }
    if (best < 0) {
      std::cerr << "Audio device matching '" << config.device << "' not found; available:\n";
      for (ma_uint32 i = 0; i < deviceCount; ++i) {
        std::cerr << "  [" << i << "] " << infos[i].name << "\n";
      }
      stop();
      return false;
    }
    impl_->playbackId = infos[chosen].id;
    deviceConfig.playback.pDeviceID = &impl_->playbackId;
  }
  deviceConfig.dataCallback = [](ma_device* device, void* output, const void* /*input*/,
                                 ma_uint32 frameCount) {
    auto* impl = static_cast<Impl*>(device->pUserData);
    if (!impl || !impl->self) return;
    auto* out = static_cast<float*>(output);
    if (impl->scratchL.size() < frameCount) {
      // Should not allocate in callback — clamp to prepared size
      frameCount = static_cast<ma_uint32>(impl->scratchL.size());
    }
    impl->self->synth_.process(impl->scratchL.data(), impl->scratchR.data(),
                               static_cast<int>(frameCount));
    for (ma_uint32 i = 0; i < frameCount; ++i) {
      out[i * 2 + 0] = impl->scratchL[i];
      out[i * 2 + 1] = impl->scratchR[i];
    }
  };

  if (ma_device_init(&impl_->context, &deviceConfig, &impl_->device) != MA_SUCCESS) {
    std::cerr << "Failed to initialize audio device\n";
    stop();
    return false;
  }
  impl_->deviceInitialized = true;

  char name[256];
  if (ma_device_get_name(&impl_->device, ma_device_type_playback, name, sizeof(name), nullptr) ==
      MA_SUCCESS) {
    deviceName_ = name;
  } else {
    deviceName_ = "default";
  }

  if (ma_device_start(&impl_->device) != MA_SUCCESS) {
    std::cerr << "Failed to start audio device\n";
    stop();
    return false;
  }

  running_.store(true);
  return true;
}

void AudioEngine::stop() {
  running_.store(false);
  if (!impl_) return;
  if (impl_->deviceInitialized) {
    ma_device_uninit(&impl_->device);
    impl_->deviceInitialized = false;
  }
  if (impl_->contextInitialized) {
    ma_context_uninit(&impl_->context);
    impl_->contextInitialized = false;
  }
  deviceName_.clear();
}

void AudioEngine::processBlock(float* interleavedStereo, int numFrames) {
  if (impl_->scratchL.size() < static_cast<size_t>(numFrames)) {
    impl_->scratchL.resize(static_cast<size_t>(numFrames));
    impl_->scratchR.resize(static_cast<size_t>(numFrames));
  }
  synth_.process(impl_->scratchL.data(), impl_->scratchR.data(), numFrames);
  for (int i = 0; i < numFrames; ++i) {
    interleavedStereo[i * 2 + 0] = impl_->scratchL[static_cast<size_t>(i)];
    interleavedStereo[i * 2 + 1] = impl_->scratchR[static_cast<size_t>(i)];
  }
}

}  // namespace glow
