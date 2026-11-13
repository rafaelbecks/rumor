#pragma once

#include <JuceHeader.h>

#include <functional>

/** Listens for OSC messages and reports the first numeric argument of each one. */
class OscInput : private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback> {
 public:
  using Handler = std::function<void(const juce::String& address, float value)>;

  OscInput();
  ~OscInput() override;

  void setHandler(Handler handler);
  bool listen(const juce::String& host, int port, juce::String& error);
  void stop();
  bool isListening() const { return listening_; }
  juce::String status() const { return status_; }

 private:
  void oscMessageReceived(const juce::OSCMessage& message) override;
  void oscBundleReceived(const juce::OSCBundle& bundle) override;
  void handleMessage(const juce::OSCMessage& message);

  juce::OSCReceiver receiver_{"RUMOR OSC"};
  std::unique_ptr<juce::DatagramSocket> socket_;
  Handler handler_;
  bool listening_{false};
  juce::String status_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscInput)
};
