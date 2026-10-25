#pragma once

#include <JuceHeader.h>

#include <functional>

class UmbralAudioProcessor;

/** Modal sections for serial, websocket keys, and the remote host. */
class SensorConfigComponent : public juce::Component, private juce::Button::Listener, private juce::Timer {
 public:
  SensorConfigComponent(UmbralAudioProcessor& processor, juce::Font font, std::function<void()> onSaved);
  ~SensorConfigComponent() override;

  void paint(juce::Graphics& g) override;
  void resized() override;

 private:
  struct KeyRow : public juce::Component {
    KeyRow();
    void resized() override;
    juce::ToggleButton enable;
    juce::Label name;
    juce::Label reading;
    juce::TextEditor low;
    juce::TextEditor high;
  };

  void buttonClicked(juce::Button* button) override;
  void timerCallback() override;
  void styleEditor(juce::TextEditor& editor, const juce::String& empty);
  void refreshPorts();
  void rebuildKeys();
  bool collect(juce::String& error);
  void closeWith(int code);

  UmbralAudioProcessor& processor_;
  juce::Font font_;
  std::function<void()> onSaved_;

  juce::Label serialTitle_;
  juce::Label portLabel_;
  juce::ComboBox portBox_;
  juce::TextButton refreshButton_;
  juce::Label baudLabel_;
  juce::ComboBox baudBox_;
  juce::Label inputLabel_;
  juce::TextEditor inputLow_;
  juce::TextEditor inputHigh_;
  juce::StringArray portPaths_;

  juce::Label wsTitle_;
  juce::Label hostLabel_;
  juce::TextEditor hostEdit_;
  juce::Viewport keyViewport_;
  juce::Label keyHint_;
  juce::Component keyList_;
  std::vector<std::unique_ptr<KeyRow>> keyRows_;
  juce::String keySignature_;

  juce::Label remoteTitle_;
  juce::Label remoteLabel_;
  juce::TextEditor remoteEdit_;
  juce::TextButton remoteButton_;

  juce::Label errorLabel_;
  juce::TextButton saveButton_;
  juce::TextButton closeButton_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SensorConfigComponent)
};
