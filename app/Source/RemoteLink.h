#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>

/** Outbound WebSocket to the RUMOR remote hub (`ws://` or `wss://`, from the pasted URL). */
inline constexpr const char* kDefaultRemoteUrl = "https://rumor-remote.onrender.com";

class RemoteLink : private juce::Thread {
 public:
  using HelloFn = std::function<juce::String()>;
  using StateFn = std::function<juce::String()>;
  using CommandFn = std::function<void(const juce::var&)>;

  RemoteLink();
  ~RemoteLink() override;

  void start(HelloFn hello, StateFn state, CommandFn onCommand);
  void stop();

  /** Empty URL drops the socket. A new URL reconnects. */
  void setUrl(const juce::String& url);

  bool isConnected() const { return connected_.load(); }
  juce::String status() const;

  void pushDebug(const juce::String& line);

 private:
  void run() override;

  HelloFn hello_;
  StateFn state_;
  CommandFn onCommand_;
  std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

  juce::CriticalSection urlLock_;
  juce::String url_;
  std::atomic<uint32_t> epoch_{0};

  juce::CriticalSection statusLock_;
  juce::String status_;

  juce::CriticalSection debugLock_;
  std::deque<juce::String> debugLines_;

  std::atomic<bool> connected_{false};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RemoteLink)
};
