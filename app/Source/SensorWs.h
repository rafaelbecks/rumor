#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <functional>
#include <utility>
#include <vector>

/** Outbound WebSocket that receives JSON sensor frames (ws:// or wss://). */
class SensorWs : private juce::Thread {
 public:
  using Sample = std::pair<std::string, float>;
  using ValuesFn = std::function<void(const std::vector<Sample>&, const juce::String& pretty)>;
  using ConnectFn = std::function<void(bool connected)>;

  SensorWs();
  ~SensorWs() override;

  void start(ValuesFn onValues, ConnectFn onConnection);
  void stop();

  /** Empty URL drops the socket. A new URL reconnects. */
  void setUrl(const juce::String& url);

  bool isConnected() const { return connected_.load(); }
  juce::String status() const;

 private:
  void run() override;

  ValuesFn onValues_;
  ConnectFn onConnection_;

  juce::CriticalSection urlLock_;
  juce::String url_;
  std::atomic<uint32_t> epoch_{0};

  juce::CriticalSection statusLock_;
  juce::String status_;

  std::atomic<bool> connected_{false};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SensorWs)
};
