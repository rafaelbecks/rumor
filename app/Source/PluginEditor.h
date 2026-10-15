#pragma once

#include "PluginProcessor.h"
#include "PaulXStretchPanel.h"

#include <functional>
#include <memory>
#include <vector>

class UmbralAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   private juce::Timer,
                                   private juce::Button::Listener,
                                   private juce::Slider::Listener,
                                   private juce::ComboBox::Listener,
                                   private juce::TextEditor::Listener {
 public:
  explicit UmbralAudioProcessorEditor(UmbralAudioProcessor&);
  ~UmbralAudioProcessorEditor() override;

  void paint(juce::Graphics&) override;
  void resized() override;
  void parentHierarchyChanged() override;

 private:
  struct GlassPanel : public juce::Component {
    explicit GlassPanel(const juce::String& title);
    void paint(juce::Graphics& g) override;
    juce::Rectangle<int> contentBounds() const;
    juce::Label title;
  };

  /** One-cycle waveform preview (GRM Gen-style), driven by osc.waveform + osc.timbre. */
  struct WaveformGraph : public juce::Component {
    void setShape(float waveformPos, float sineTimbre);
    void paint(juce::Graphics& g) override;

   private:
    float waveformPos_{0.0f};
    float sineTimbre_{0.0f};
  };

  struct DestinationRow : public juce::Component,
                          private juce::Button::Listener,
                          private juce::ComboBox::Listener {
    DestinationRow();
    void resized() override;
    void buttonClicked(juce::Button*) override;
    void comboBoxChanged(juce::ComboBox*) override;
    void applyDefaultRangeForSelection();

    std::function<void(DestinationRow*)> onRemove;
    juce::ComboBox parameterBox;
    juce::Label ccLabel;
    juce::Slider ccSlider;
    juce::TextEditor minEdit;  // typed output range lo
    juce::TextEditor maxEdit;  // typed output range hi
    juce::ToggleButton invertToggle;
    juce::TextButton removeButton;
  };

  struct ParamSlider {
    glow::ParameterID id{};
    juce::Label label;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
  };

  void timerCallback() override;
  void buttonClicked(juce::Button* button) override;
  void sliderValueChanged(juce::Slider* slider) override;
  void comboBoxChanged(juce::ComboBox* box) override;
  void textEditorTextChanged(juce::TextEditor&) override;

  void refreshSerialPorts();
  void refreshMidiOutputs();
  void refreshPresetList();
  void refreshEngineUi();
  void loadSelectedPreset();
  void saveCurrentAsPreset();
  void syncSlidersFromEngine();
  void updateDroneReadout();
  void updateParamLockUi();
  void setStatus(const juce::String& text, bool isError = false);
  void loadConfigIntoUi(const glow::SerialConfig& cfg);
  glow::SerialConfig buildConfigFromUi() const;
  void addDestinationRow(const glow::SerialRoute& route);
  void layoutDestinationRows();
  bool applyFromUi(bool connectSerial);
  void makeParamSliders(const glow::ParameterID* ids, size_t count,
                        std::vector<std::unique_ptr<ParamSlider>>& out);
  void appendDebugChunk(const juce::String& chunk);
  void rebuildDebugView();
  void styleCuteControls();
  void applyBrandFont(juce::Component& root);
  void updateWaveGraph();
  void layoutSintePage();

  UmbralAudioProcessor& processor_;
  juce::Image background_;
  juce::Typeface::Ptr analoGrotesk_;
  juce::Typeface::Ptr latoThin_;
  juce::Typeface::Ptr latoLight_;
  juce::Typeface::Ptr latoRegular_;
  juce::Font titleFont_{juce::FontOptions(36.0f)};  // Analo Grotesk — RUMOR wordmark
  juce::Font brandFont_{juce::FontOptions(13.0f)};   // Lato Light (UI)
  juce::Font bodyFont_{juce::FontOptions(12.5f)};    // Lato Regular
  std::unique_ptr<juce::LookAndFeel> atelierLaf_;

  juce::Label titleLabel_;
  juce::Label subtitleLabel_;
  juce::Label statusLabel_;
  juce::ComboBox presetBox_;
  juce::TextButton savePresetButton_;
  juce::TextButton engineButton_;
  juce::ToggleButton granularToggle_;  // sinte engine enable
  juce::ToggleButton pxToggle_;

  struct SintePageHost : juce::Component {
    std::function<void()> onResized;
    void resized() override {
      if (onResized) onResized();
    }
  };

  /** Outer module card (Atelier-style separated panel). */
  struct ModuleFrame : public juce::Component {
    explicit ModuleFrame(float fillAlpha = 0.55f) : fillAlpha_(fillAlpha) {}
    void paint(juce::Graphics& g) override;
    juce::Rectangle<int> contentBounds() const;
    float fillAlpha_{0.55f};
  };

  ModuleFrame leftModule_{0.38f};   // outer shell (lighter)
  ModuleFrame rightModule_{0.28f};  // outer shell (more open); inner panels use full glass
  juce::TabbedComponent leftTabs_{juce::TabbedButtonBar::TabsAtBottom};
  juce::TabbedComponent rightTabs_{juce::TabbedButtonBar::TabsAtBottom};
  SintePageHost sintePage_;
  GlassPanel serialPanel_;
  GlassPanel synthPanel_;
  GlassPanel modPanel_;
  GlassPanel outPanel_;
  std::unique_ptr<PaulXStretchPanel> pxPanel_;

  juce::TextButton applyButton_;
  juce::TextButton connectButton_;
  juce::TextButton disconnectButton_;
  juce::TextButton refreshPortsButton_;
  juce::TextButton addDestButton_;
  juce::ToggleButton debugToggle_;

  juce::Label portLabel_;
  juce::ComboBox portBox_;
  juce::Label baudLabel_;
  juce::ComboBox baudBox_;
  juce::Label midiOutLabel_;
  juce::ComboBox midiOutBox_;
  juce::Label inputLabel_;
  juce::TextEditor inputLowEdit_;   // serial ADC window lo (typed)
  juce::TextEditor inputHighEdit_;  // serial ADC window hi (typed)
  juce::Label destSectionLabel_;
  juce::Component destList_;
  std::vector<std::unique_ptr<DestinationRow>> destRows_;
  juce::Label filterLabel_;
  juce::TextEditor debugFilter_;
  juce::TextEditor debugConsole_;
  juce::StringArray debugHistory_;

  juce::Label droneLabel_;
  juce::Slider droneSlider_;
  juce::Label droneReadout_;
  juce::Label waveLabel_;
  juce::ComboBox waveBox_;
  juce::Label quantLabel_;
  juce::ComboBox quantBox_;
  WaveformGraph waveGraph_;
  std::vector<std::unique_ptr<ParamSlider>> synthSliders_;
  std::vector<std::unique_ptr<ParamSlider>> modSliders_;

  juce::Label routeTitle_;
  juce::Label granRouteLabel_;
  juce::Label granLevelLabel_;
  juce::Slider granLevelSlider_;
  juce::Label granBalLabel_;
  juce::Slider granBalSlider_;
  juce::Label pxRouteLabel_;
  juce::Label pxLevelLabel_;
  juce::Slider pxLevelSlider_;
  juce::Label pxBalLabel_;
  juce::Slider pxBalSlider_;
  juce::Label masterLabel_;
  juce::Label balanceLabel_;
  juce::Slider balanceSlider_;
  juce::Label balanceHint_;
  juce::Label gainLLabel_;
  juce::Slider gainLSlider_;
  juce::Label gainRLabel_;
  juce::Slider gainRSlider_;
  juce::ToggleButton swapOutToggle_;
  juce::ToggleButton monoSumToggle_;
  juce::TextButton soloLButton_;
  juce::TextButton soloRButton_;
  juce::TextButton bothOutButton_;

  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> granLevelAttach_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> granBalAttach_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pxLevelAttach_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pxBalAttach_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> balanceAttach_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainLAttach_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainRAttach_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> swapAttach_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoAttach_;

  juce::StringArray portPaths_;
  juce::StringArray midiOutIds_;
  juce::String currentPresetName_;
  bool syncingUi_{false};
  bool ignorePresetChange_{false};
  bool ignoreMidiOutChange_{false};
  bool ignoreEngineChange_{false};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UmbralAudioProcessorEditor)
};
