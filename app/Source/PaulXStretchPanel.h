#pragma once

#include "PluginProcessor.h"

#include <memory>
#include <vector>

/** Panel de control PaulXStretch (misma instancia del motor). */
class PaulXStretchPanel : public juce::Component,
                          private juce::Timer,
                          private juce::Button::Listener,
                          private juce::Slider::Listener {
 public:
  explicit PaulXStretchPanel(UmbralAudioProcessor& processor);
  ~PaulXStretchPanel() override;

  void paint(juce::Graphics& g) override;
  void resized() override;
  void refreshFromEngine();
  void setBrandFont(const juce::Font& font);
  /** Hide panel title when embedded in a tab bar. */
  void setShowTitle(bool show);

 private:
  struct SliderRow {
    juce::String paramId;
    juce::Label label;
    juce::Slider slider;
  };

  struct ToggleRow {
    juce::String paramId;
    juce::ToggleButton toggle;
  };

  /** Source audio overview with draggable sound start/end (PaulXStretch-style). */
  class SourceWaveform : public juce::Component, private juce::ChangeListener {
   public:
    explicit SourceWaveform(UmbralAudioProcessor& processor);
    ~SourceWaveform() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void reloadFromProcessor();
    void setPlayhead(float norm01);
    void setRange(float start01, float end01);

   private:
    enum class DragTarget { None, Start, End, Region };

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    float xToNorm(float x) const;
    float normToX(float n) const;
    DragTarget hitHandle(float x) const;
    void applyRangeToProcessor();

    UmbralAudioProcessor& processor_;
    juce::AudioFormatManager formatManager_;
    juce::AudioThumbnailCache thumbCache_{8};
    juce::AudioThumbnail thumbnail_{512, formatManager_, thumbCache_};
    float start_{0.0f};
    float end_{1.0f};
    float playhead_{0.0f};
    DragTarget drag_{DragTarget::None};
    float dragGrabOffset_{0.0f};
  };

  void timerCallback() override;
  void buttonClicked(juce::Button* button) override;
  void sliderValueChanged(juce::Slider* slider) override;

  void chooseFile();
  void syncSerialDrivenSliders();
  void pushSlider(SliderRow& row);
  void pushToggle(ToggleRow& row);
  void addSlider(const juce::String& id, const juce::String& label, double minV, double maxV,
                 double step);
  void addToggle(const juce::String& id, const juce::String& label);
  SliderRow* findSlider(const juce::String& id);

  UmbralAudioProcessor& processor_;
  juce::Font brandFont_{juce::FontOptions(13.0f)};

  juce::Label title_;
  juce::Label subtitle_;
  juce::Label sourceLabel_;
  juce::Label playheadLabel_;
  juce::TextButton loadButton_;
  juce::TextButton clearButton_;
  juce::TextButton useEngineButton_;
  juce::ToggleButton freezeToggle_;
  juce::ToggleButton pauseToggle_;
  juce::ToggleButton dryToggle_;

  juce::Label modulesLabel_;
  std::vector<std::unique_ptr<SliderRow>> sliders_;
  std::vector<std::unique_ptr<ToggleRow>> toggles_;
  std::unique_ptr<SourceWaveform> sourceWaveform_;

  std::unique_ptr<juce::FileChooser> fileChooser_;
  bool syncing_{false};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PaulXStretchPanel)
};
