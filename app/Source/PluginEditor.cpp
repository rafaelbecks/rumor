#include "PluginEditor.h"
#include "Presets.h"
#include "BinaryData.h"
#include "audio/Oscillator.h"
#include "util/Scale.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr int kEditorWidth = 1100;
constexpr int kEditorHeight = 780;
constexpr int kHeaderH = 40;  // compact toolbar + RUMOR wordmark
constexpr int kFooterH = 44;
constexpr int kRowH = 26;
constexpr int kDestRowH = 26;
constexpr int kGap = 12;
constexpr int kPanelPad = 10;
constexpr int kModulePad = 8;

constexpr glow::ParameterID kSynthParams[] = {
    glow::ParameterID::SineTimbre,      glow::ParameterID::Amplitude,
    glow::ParameterID::FilterCutoff,    glow::ParameterID::FilterResonance,
    glow::ParameterID::Attack,          glow::ParameterID::Release,
};

constexpr glow::ParameterID kModParams[] = {
    glow::ParameterID::ChordSpread,  glow::ParameterID::FolderDrive,
    glow::ParameterID::FolderMix,    glow::ParameterID::GrainMix,
    glow::ParameterID::GrainDensity, glow::ParameterID::GrainPitch,
};

constexpr const char* kDestNames[] = {
    "pitch",           "midi.cc",         "amplitude",      "filter.cutoff",
    "filter.resonance","env.attack",      "env.release",    "osc.waveform",
    "osc.timbre",      "chord.spread",    "folder.drive",   "folder.mix",
    "granular.mix",    "granular.density","granular.pitch", "px.stretch",
    "px.pitch_shift",  "px.freq_shift",   "px.main_volume", "px.fft_size",
    "px.spread",
};
constexpr int kDestNameCount = static_cast<int>(sizeof(kDestNames) / sizeof(kDestNames[0]));

juce::String U8(const char* utf8) { return juce::String::fromUTF8(utf8); }

// GRM Atelier–inspired palette: flat charcoal, soft blue accent, lowercase calm.
const juce::Colour kBg{0xff141416};
const juce::Colour kGlass{0xe61c1c1f};
const juce::Colour kGlassStroke{0x22ffffff};
const juce::Colour kAccent{0xffc4a0ff};  // previous rumor purple (near icon)
const juce::Colour kText{0xffe8e8ea};
const juce::Colour kMuted{0xff8a8a92};

void styleLabel(juce::Label& label, const juce::Font& font, float alpha = 1.0f) {
  label.setFont(font);
  label.setColour(juce::Label::textColourId, kText.withAlpha(alpha));
  label.setJustificationType(juce::Justification::centredLeft);
}

void setupCompactSlider(juce::Slider& s, double minV, double maxV, double interval) {
  s.setRange(minV, maxV, interval);
  s.setSliderStyle(juce::Slider::LinearHorizontal);
  s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 16);
  s.setColour(juce::Slider::thumbColourId, juce::Colours::white);
  s.setColour(juce::Slider::trackColourId, kAccent.withAlpha(0.75f));
  s.setColour(juce::Slider::backgroundColourId, juce::Colour(0x33ffffff));
  s.setColour(juce::Slider::textBoxTextColourId, kMuted);
  s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
  s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
}

void setupCompactEdit(juce::TextEditor& e, const juce::String& placeholder = {}) {
  e.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0x14ffffff));
  e.setColour(juce::TextEditor::textColourId, kText);
  e.setColour(juce::TextEditor::outlineColourId, kGlassStroke);
  e.setColour(juce::TextEditor::focusedOutlineColourId, kAccent.withAlpha(0.55f));
  e.setColour(juce::TextEditor::highlightedTextColourId, kText);
  e.setJustification(juce::Justification::centred);
  e.setInputRestrictions(24, "0123456789.-+eE");
  if (placeholder.isNotEmpty()) e.setTextToShowWhenEmpty(placeholder, kMuted);
}

/** Thin track + soft blue fill + white pill thumb (Atelier-like). */
class AtelierLookAndFeel : public juce::LookAndFeel_V4 {
 public:
  void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                        float /*minSliderPos*/, float /*maxSliderPos*/,
                        const juce::Slider::SliderStyle style, juce::Slider& slider) override {
    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar &&
        style != juce::Slider::LinearBarVertical) {
      LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, 0, 0, style, slider);
      return;
    }
    const float trackH = 2.0f;
    const float cy = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    const float left = static_cast<float>(x) + 4.0f;
    const float right = static_cast<float>(x + width) - 4.0f;
    const float thumbX = juce::jlimit(left, right, sliderPos);

    g.setColour(juce::Colour(0x33ffffff));
    g.fillRoundedRectangle(left, cy - trackH * 0.5f, right - left, trackH, 1.0f);
    g.setColour(kAccent.withAlpha(0.7f));
    g.fillRoundedRectangle(left, cy - trackH * 0.5f, juce::jmax(0.0f, thumbX - left), trackH, 1.0f);

    g.setColour(juce::Colours::white);
    g.fillRoundedRectangle(thumbX - 5.0f, cy - 5.0f, 10.0f, 10.0f, 5.0f);
  }

  void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                            const juce::Colour& /*backgroundColour*/, bool shouldDrawButtonAsHighlighted,
                            bool shouldDrawButtonAsDown) override {
    auto r = button.getLocalBounds().toFloat().reduced(0.5f);
    auto fill = juce::Colour(0x14ffffff);
    if (shouldDrawButtonAsDown) fill = kAccent.withAlpha(0.35f);
    else if (shouldDrawButtonAsHighlighted) fill = juce::Colour(0x22ffffff);
    if (button.getToggleState()) fill = kAccent.withAlpha(0.28f);
    g.setColour(fill);
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(kGlassStroke);
    g.drawRoundedRectangle(r, 6.0f, 1.0f);
  }

  void drawComboBox(juce::Graphics& g, int width, int height, bool /*isButtonDown*/, int /*buttonX*/,
                    int /*buttonY*/, int /*buttonW*/, int /*buttonH*/, juce::ComboBox& box) override {
    auto r = juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width) - 1.0f,
                                    static_cast<float>(height) - 1.0f);
    g.setColour(juce::Colour(0x14ffffff));
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(box.hasKeyboardFocus(true) ? kAccent.withAlpha(0.6f) : kGlassStroke);
    g.drawRoundedRectangle(r, 5.0f, 1.0f);

    juce::Rectangle<int> arrowZone(width - 22, 0, 16, height);
    juce::Path path;
    path.startNewSubPath(static_cast<float>(arrowZone.getX()) + 3.0f,
                         static_cast<float>(arrowZone.getCentreY()) - 2.0f);
    path.lineTo(static_cast<float>(arrowZone.getCentreX()),
                static_cast<float>(arrowZone.getCentreY()) + 3.0f);
    path.lineTo(static_cast<float>(arrowZone.getRight()) - 3.0f,
                static_cast<float>(arrowZone.getCentreY()) - 2.0f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId)
                    .withAlpha(box.isEnabled() ? 0.85f : 0.3f));
    g.strokePath(path, juce::PathStrokeType(1.5f));
  }

  void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
    label.setBounds(8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, box.findColour(juce::ComboBox::textColourId));
  }

  void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override {
    g.setColour(juce::Colour(0xf01c1c1f));
    g.fillRoundedRectangle(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 6.0f);
    g.setColour(kGlassStroke);
    g.drawRoundedRectangle(0.5f, 0.5f, static_cast<float>(width) - 1.0f,
                           static_cast<float>(height) - 1.0f, 6.0f, 1.0f);
  }

  void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                         bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                         const juce::String& text, const juce::String& shortcutKeyText,
                         const juce::Drawable* icon, const juce::Colour* textColourToUse) override {
    if (isSeparator) {
      auto r = area.reduced(8, 0);
      g.setColour(juce::Colour(0x22ffffff));
      g.fillRect(r.withHeight(1).withY(r.getCentreY()));
      return;
    }

    auto r = area.reduced(2, 1);
    if (isHighlighted && isActive) {
      g.setColour(kAccent.withAlpha(0.28f));
      g.fillRoundedRectangle(r.toFloat(), 4.0f);
    }

    auto textColour = textColourToUse != nullptr ? *textColourToUse
                                                 : (isActive ? kText : kMuted);
    g.setColour(isHighlighted ? kText : textColour);

    auto font = getPopupMenuFont();
    font = font.withHeight(juce::jmin(font.getHeight(), static_cast<float>(r.getHeight()) * 0.7f));
    g.setFont(font);

    auto textR = r.reduced(10, 0);
    if (isTicked) {
      g.setColour(kAccent);
      g.fillEllipse(static_cast<float>(textR.getX()), static_cast<float>(r.getCentreY()) - 3.0f, 6.0f,
                    6.0f);
      textR.removeFromLeft(14);
      g.setColour(isHighlighted ? kText : textColour);
    }
    g.drawFittedText(text, textR, juce::Justification::centredLeft, 1);

    juce::ignoreUnused(hasSubMenu, shortcutKeyText, icon);
  }

  juce::Typeface::Ptr light;
  juce::Typeface::Ptr regular;

  juce::Font uiFont(float height) const {
    if (light != nullptr) return juce::Font(juce::FontOptions{light}).withHeight(height);
    if (regular != nullptr) return juce::Font(juce::FontOptions{regular}).withHeight(height);
    return juce::Font(juce::FontOptions(height));
  }

  juce::Font getComboBoxFont(juce::ComboBox&) override { return uiFont(12.5f); }
  juce::Font getPopupMenuFont() override { return uiFont(12.5f); }

  juce::Font getTextButtonFont(juce::TextButton&, int) override { return uiFont(12.5f); }

  juce::Font getLabelFont(juce::Label&) override { return uiFont(12.5f); }

  juce::Font getTabButtonFont(juce::TabBarButton&, float height) override {
    return uiFont(height * 0.55f);
  }

  void drawTabButton(juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver,
                     bool isMouseDown) override {
    const bool active = button.isFrontTab();
    auto r = button.getActiveArea().toFloat().reduced(2.0f, 3.0f);
    auto fill = juce::Colour(0x10ffffff);
    if (active) fill = juce::Colour(0x22ffffff);
    else if (isMouseOver || isMouseDown) fill = juce::Colour(0x18ffffff);
    g.setColour(fill);
    g.fillRoundedRectangle(r, 7.0f);
    g.setColour(active ? juce::Colours::white.withAlpha(0.55f) : kGlassStroke);
    g.drawRoundedRectangle(r, 7.0f, active ? 1.2f : 1.0f);

    auto textArea = button.getActiveArea().reduced(4, 2);
    g.setColour(active ? kText : kMuted);
    g.setFont(getTabButtonFont(button, static_cast<float>(button.getHeight())));
    g.drawFittedText(button.getButtonText(), textArea, juce::Justification::centred, 1);
  }

  int getTabButtonOverlap(int /*tabDepth*/) override { return 0; }
  int getTabButtonSpaceAroundImage() override { return 0; }
};

}  // namespace

void UmbralAudioProcessorEditor::ModuleFrame::paint(juce::Graphics& g) {
  auto r = getLocalBounds().toFloat();
  // Base panel colour with per-module alpha so left/right read differently.
  g.setColour(juce::Colour(0x1c1c1f).withAlpha(juce::jlimit(0.15f, 0.85f, fillAlpha_)));
  g.fillRoundedRectangle(r, 12.0f);
  g.setColour(kGlassStroke.withAlpha(0.35f + fillAlpha_ * 0.25f));
  g.drawRoundedRectangle(r.reduced(0.5f), 12.0f, 1.0f);
}

juce::Rectangle<int> UmbralAudioProcessorEditor::ModuleFrame::contentBounds() const {
  return getLocalBounds().reduced(kModulePad);
}

UmbralAudioProcessorEditor::GlassPanel::GlassPanel(const juce::String& titleText) {
  title.setText(titleText, juce::dontSendNotification);
  title.setJustificationType(juce::Justification::centredLeft);
  title.setColour(juce::Label::textColourId, kMuted);
  addAndMakeVisible(title);
}

void UmbralAudioProcessorEditor::GlassPanel::paint(juce::Graphics& g) {
  // Same inner glass treatment as PaulXStretchPanel.
  auto r = getLocalBounds().toFloat();
  g.setColour(kGlass);
  g.fillRoundedRectangle(r, 8.0f);
  g.setColour(kGlassStroke);
  g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);
}

void UmbralAudioProcessorEditor::WaveformGraph::setShape(float waveformPos, float sineTimbre) {
  if (std::abs(waveformPos_ - waveformPos) < 0.0005f &&
      std::abs(sineTimbre_ - sineTimbre) < 0.0005f)
    return;
  waveformPos_ = waveformPos;
  sineTimbre_ = sineTimbre;
  repaint();
}

void UmbralAudioProcessorEditor::WaveformGraph::paint(juce::Graphics& g) {
  auto r = getLocalBounds().toFloat().reduced(1.0f);
  g.setColour(juce::Colour(0x22000000));
  g.fillRoundedRectangle(r, 6.0f);
  g.setColour(kGlassStroke);
  g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);

  auto plot = r.reduced(8.0f, 6.0f);
  const float midY = plot.getCentreY();
  g.setColour(juce::Colour(0x18ffffff));
  g.drawHorizontalLine(juce::roundToInt(midY), plot.getX(), plot.getRight());

  // Ghost trails (grmgen-style overlapping waves) then solid primary.
  constexpr int kPoints = 256;
  constexpr float kPeriods = 4.0f;
  const float ghostOffsets[] = {0.08f, 0.04f, 0.0f};
  const float ghostAlphas[] = {0.18f, 0.35f, 1.0f};
  for (int layer = 0; layer < 3; ++layer) {
    juce::Path path;
    for (int i = 0; i < kPoints; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(kPoints - 1);
      const float phase = std::fmod(t * kPeriods + ghostOffsets[layer] * sineTimbre_, 1.0f);
      const float sample = glow::morphWaveformSample(waveformPos_, phase, sineTimbre_);
      const float x = plot.getX() + t * plot.getWidth();
      const float y = midY - sample * (plot.getHeight() * 0.42f);
      if (i == 0)
        path.startNewSubPath(x, y);
      else
        path.lineTo(x, y);
    }
    const auto col = (layer == 2) ? juce::Colours::white : kAccent;
    g.setColour(col.withAlpha(ghostAlphas[layer]));
    g.strokePath(path, juce::PathStrokeType(layer == 2 ? 1.4f : 1.1f,
                                            juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
  }
}

juce::Rectangle<int> UmbralAudioProcessorEditor::GlassPanel::contentBounds() const {
  auto r = getLocalBounds().reduced(kPanelPad);
  if (title.isVisible() && title.getText().isNotEmpty())
    return r.withTrimmedTop(22);
  return r;
}

UmbralAudioProcessorEditor::DestinationRow::DestinationRow()
    : ccLabel({}, "CC"), invertToggle(U8("Inv")), removeButton("-") {
  for (int i = 0; i < kDestNameCount; ++i) parameterBox.addItem(kDestNames[i], i + 1);
  parameterBox.setSelectedItemIndex(0, juce::dontSendNotification);
  parameterBox.addListener(this);
  parameterBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0x14ffffff));
  parameterBox.setColour(juce::ComboBox::outlineColourId, kGlassStroke);
  parameterBox.setColour(juce::ComboBox::textColourId, kText);
  parameterBox.setColour(juce::ComboBox::arrowColourId, kMuted);
  addAndMakeVisible(parameterBox);
  ccLabel.setColour(juce::Label::textColourId, kMuted);
  addAndMakeVisible(ccLabel);
  setupCompactSlider(ccSlider, 0, 127, 1);
  ccSlider.setNumDecimalPlacesToDisplay(0);
  ccSlider.setValue(1, juce::dontSendNotification);
  addAndMakeVisible(ccSlider);
  setupCompactEdit(minEdit, "min");
  setupCompactEdit(maxEdit, "max");
  addAndMakeVisible(minEdit);
  addAndMakeVisible(maxEdit);
  addAndMakeVisible(invertToggle);
  removeButton.addListener(this);
  addAndMakeVisible(removeButton);
  applyDefaultRangeForSelection();
}

void UmbralAudioProcessorEditor::DestinationRow::applyDefaultRangeForSelection() {
  glow::SerialDestination dest = glow::SerialDestination::ChordSpread;
  glow::serialDestinationFromName(parameterBox.getText().toStdString(), dest);
  const bool isCc = dest == glow::SerialDestination::MidiCc;
  ccLabel.setVisible(isCc);
  ccSlider.setVisible(isCc);
  float lo = 0.0f, hi = 1.0f;
  glow::serialDestinationDefaultRange(dest, lo, hi);
  minEdit.setText(juce::String(lo, 4), false);
  maxEdit.setText(juce::String(hi, 4), false);
  resized();
}

void UmbralAudioProcessorEditor::DestinationRow::resized() {
  auto r = getLocalBounds();
  parameterBox.setBounds(r.removeFromLeft(110));
  r.removeFromLeft(4);
  if (ccSlider.isVisible()) {
    ccLabel.setBounds(r.removeFromLeft(22));
    ccSlider.setBounds(r.removeFromLeft(56));
    r.removeFromLeft(4);
  }
  minEdit.setBounds(r.removeFromLeft(72));
  r.removeFromLeft(4);
  maxEdit.setBounds(r.removeFromLeft(72));
  r.removeFromLeft(4);
  invertToggle.setBounds(r.removeFromLeft(44));
  r.removeFromLeft(4);
  removeButton.setBounds(r.removeFromLeft(28));
}

void UmbralAudioProcessorEditor::DestinationRow::buttonClicked(juce::Button* b) {
  if (b == &removeButton && onRemove) onRemove(this);
}

void UmbralAudioProcessorEditor::DestinationRow::comboBoxChanged(juce::ComboBox*) {
  applyDefaultRangeForSelection();
}

void UmbralAudioProcessorEditor::makeParamSliders(
    const glow::ParameterID* ids, size_t count, std::vector<std::unique_ptr<ParamSlider>>& out) {
  for (size_t i = 0; i < count; ++i) {
    auto row = std::make_unique<ParamSlider>();
    row->id = ids[i];
    const auto& desc = glow::parameterDesc(ids[i]);
    juce::String labelText = desc.name;
    if (ids[i] == glow::ParameterID::SineTimbre) labelText = "timbre";
    else if (ids[i] == glow::ParameterID::Amplitude) labelText = "gain";
    else if (ids[i] == glow::ParameterID::FilterCutoff) labelText = "cutoff";
    else if (ids[i] == glow::ParameterID::FilterResonance) labelText = "resonance";
    else if (ids[i] == glow::ParameterID::Attack) labelText = "attack";
    else if (ids[i] == glow::ParameterID::Release) labelText = "release";
    else if (ids[i] == glow::ParameterID::ChordSpread) labelText = "spread";
    else if (ids[i] == glow::ParameterID::FolderDrive) labelText = "folder";
    else if (ids[i] == glow::ParameterID::FolderMix) labelText = "folder mix";
    else if (ids[i] == glow::ParameterID::GrainMix) labelText = "grain mix";
    else if (ids[i] == glow::ParameterID::GrainDensity) labelText = "density";
    else if (ids[i] == glow::ParameterID::GrainPitch) labelText = "grain pitch";
    row->label.setText(labelText, juce::dontSendNotification);
    styleLabel(row->label, brandFont_, 0.92f);
    row->slider.setSliderStyle(juce::Slider::LinearHorizontal);
    row->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 16);
    row->slider.setColour(juce::Slider::thumbColourId, juce::Colours::white);
    row->slider.setColour(juce::Slider::trackColourId, kAccent.withAlpha(0.75f));
    row->slider.setColour(juce::Slider::backgroundColourId, juce::Colour(0x33ffffff));
    row->slider.setColour(juce::Slider::textBoxTextColourId, kMuted);
    row->slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    row->slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    row->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor_.apvts(), UmbralAudioProcessor::paramIdFor(ids[i]), row->slider);
    out.push_back(std::move(row));
  }
}

UmbralAudioProcessorEditor::UmbralAudioProcessorEditor(UmbralAudioProcessor& p)
    : AudioProcessorEditor(&p),
      processor_(p),
      savePresetButton_("guardar"),
      engineButton_("start"),
      granularToggle_("sinte"),
      pxToggle_("paulxstretch"),
      serialPanel_({}),
      synthPanel_({}),
      modPanel_({}),
      outPanel_({}),
      applyButton_("aplicar"),
      connectButton_("conectar"),
      disconnectButton_("desconectar"),
      refreshPortsButton_("actualizar"),
      addDestButton_("agregar"),
      debugToggle_("debug"),
      remoteLabel_({}, "remoto"),
      remoteButton_("vincular"),
      portLabel_({}, "puerto"),
      baudLabel_({}, "baud"),
      midiOutLabel_({}, "midi out"),
      inputLabel_({}, "entrada"),
      destSectionLabel_({}, "destinos"),
      filterLabel_({}, "filtro"),
      droneLabel_({}, "drone"),
      waveLabel_({}, "wave"),
      quantLabel_({}, "scale"),
      routeTitle_({}, "ruteo"),
      granRouteLabel_({}, "sinte"),
      granLevelLabel_({}, "nivel"),
      granBalLabel_({}, "balance l/r"),
      pxRouteLabel_({}, "paulxstretch"),
      pxLevelLabel_({}, "nivel"),
      pxBalLabel_({}, "balance l/r"),
      masterLabel_({}, "master"),
      balanceLabel_({}, "balance"),
      balanceHint_({}, U8("l sub  \xc2\xb7  r dayton")),
      gainLLabel_({}, "l sub"),
      gainRLabel_({}, "r dayton"),
      swapOutToggle_("swap l/r"),
      monoSumToggle_(U8("mono\xe2\x86\x92pan")),
      soloLButton_("solo l"),
      soloRButton_("solo r"),
      bothOutButton_("ambos") {
  setSize(kEditorWidth, kEditorHeight);

  analoGrotesk_ = juce::Typeface::createSystemTypefaceFor(BinaryData::AnaloGroteskTrialRegular_ttf,
                                                          BinaryData::AnaloGroteskTrialRegular_ttfSize);
  latoThin_ = juce::Typeface::createSystemTypefaceFor(BinaryData::LatoThin_ttf,
                                                      BinaryData::LatoThin_ttfSize);
  latoLight_ = juce::Typeface::createSystemTypefaceFor(BinaryData::LatoLight_ttf,
                                                       BinaryData::LatoLight_ttfSize);
  latoRegular_ = juce::Typeface::createSystemTypefaceFor(BinaryData::LatoRegular_ttf,
                                                         BinaryData::LatoRegular_ttfSize);
  // Wordmark only: Analo Grotesk (UI stays Lato).
  if (analoGrotesk_ != nullptr)
    titleFont_ = juce::Font(juce::FontOptions{analoGrotesk_}).withHeight(static_cast<float>(kHeaderH));
  else if (latoLight_ != nullptr)
    titleFont_ = juce::Font(juce::FontOptions{latoLight_}).withHeight(static_cast<float>(kHeaderH));
  else
    titleFont_ = juce::Font(juce::FontOptions(static_cast<float>(kHeaderH)));
  if (latoLight_ != nullptr)
    brandFont_ = juce::Font(juce::FontOptions{latoLight_}).withHeight(13.0f);
  else
    brandFont_ = juce::Font(juce::FontOptions(13.0f));
  if (latoRegular_ != nullptr)
    bodyFont_ = juce::Font(juce::FontOptions{latoRegular_}).withHeight(12.5f);
  else
    bodyFont_ = juce::Font(juce::FontOptions(12.5f));

  auto laf = std::make_unique<AtelierLookAndFeel>();
  laf->light = latoLight_;
  laf->regular = latoRegular_;
  laf->setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xf01c1c1f));
  laf->setColour(juce::PopupMenu::textColourId, kText);
  laf->setColour(juce::PopupMenu::highlightedBackgroundColourId, kAccent.withAlpha(0.28f));
  laf->setColour(juce::PopupMenu::highlightedTextColourId, kText);
  laf->setColour(juce::ComboBox::textColourId, kText);
  laf->setColour(juce::ComboBox::arrowColourId, kMuted);
  atelierLaf_ = std::move(laf);
  setLookAndFeel(atelierLaf_.get());

  background_ = juce::ImageCache::getFromMemory(BinaryData::background_png,
                                                BinaryData::background_pngSize);

  titleLabel_.setText("RUMOR", juce::dontSendNotification);
  titleLabel_.setFont(titleFont_);
  titleLabel_.setColour(juce::Label::textColourId, kText);
  titleLabel_.setJustificationType(juce::Justification::centred);
  titleLabel_.setMinimumHorizontalScale(0.35f);
  addAndMakeVisible(titleLabel_);
  subtitleLabel_.setVisible(false);

  statusLabel_.setFont(brandFont_.withHeight(11.5f));
  statusLabel_.setColour(juce::Label::textColourId, kMuted);
  statusLabel_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(statusLabel_);

  presetBox_.addListener(this);
  addAndMakeVisible(presetBox_);
  savePresetButton_.addListener(this);
  addAndMakeVisible(savePresetButton_);
  engineButton_.addListener(this);
  addAndMakeVisible(engineButton_);

  granularToggle_.setClickingTogglesState(true);
  pxToggle_.setClickingTogglesState(true);
  granularToggle_.addListener(this);
  pxToggle_.addListener(this);
  addAndMakeVisible(granularToggle_);
  addAndMakeVisible(pxToggle_);

  auto styleTabs = [](juce::TabbedComponent& tabs) {
    tabs.setOutline(0);
    tabs.setTabBarDepth(30);
    tabs.setIndent(6);
    tabs.setColour(juce::TabbedComponent::backgroundColourId, juce::Colours::transparentBlack);
    tabs.setColour(juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
    tabs.setColour(juce::TabbedButtonBar::tabOutlineColourId, juce::Colours::transparentBlack);
    tabs.setColour(juce::TabbedButtonBar::frontOutlineColourId, juce::Colours::transparentBlack);
    tabs.setColour(juce::TabbedButtonBar::frontTextColourId, kText);
    tabs.setColour(juce::TabbedButtonBar::tabTextColourId, kMuted);
  };
  styleTabs(leftTabs_);
  styleTabs(rightTabs_);

  serialPanel_.title.setVisible(false);
  synthPanel_.title.setVisible(false);
  modPanel_.title.setVisible(false);
  outPanel_.title.setVisible(false);
  // Inner glass is redundant inside module frames.
  serialPanel_.setOpaque(false);
  synthPanel_.setOpaque(false);
  modPanel_.setOpaque(false);
  outPanel_.setOpaque(false);

  sintePage_.onResized = [this] { layoutSintePage(); };
  sintePage_.addAndMakeVisible(synthPanel_);
  sintePage_.addAndMakeVisible(modPanel_);
  leftTabs_.addTab("serial", juce::Colours::transparentBlack, &serialPanel_, false);
  leftTabs_.addTab("sinte", juce::Colours::transparentBlack, &sintePage_, false);
  leftModule_.addAndMakeVisible(leftTabs_);
  addAndMakeVisible(leftModule_);

  pxPanel_ = std::make_unique<PaulXStretchPanel>(processor_);
  pxPanel_->setShowTitle(false);
  pxPanel_->setLookAndFeel(atelierLaf_.get());
  rightTabs_.addTab("paulxstretch", juce::Colours::transparentBlack, pxPanel_.get(), false);
  rightTabs_.addTab("salida", juce::Colours::transparentBlack, &outPanel_, false);
  rightModule_.addAndMakeVisible(rightTabs_);
  addAndMakeVisible(rightModule_);

  for (auto* b : {&applyButton_, &connectButton_, &disconnectButton_, &refreshPortsButton_,
                  &addDestButton_, &remoteButton_}) {
    b->addListener(this);
    serialPanel_.addAndMakeVisible(b);
  }
  debugToggle_.setClickingTogglesState(true);
  debugToggle_.setToggleState(processor_.isSerialDebugConsole(), juce::dontSendNotification);
  debugToggle_.addListener(this);
  serialPanel_.addAndMakeVisible(debugToggle_);

  for (auto* l : {&portLabel_, &baudLabel_, &midiOutLabel_, &inputLabel_, &destSectionLabel_,
                  &filterLabel_, &remoteLabel_}) {
    styleLabel(*l, brandFont_);
    serialPanel_.addAndMakeVisible(l);
  }
  remoteUrlEdit_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0x14ffffff));
  remoteUrlEdit_.setColour(juce::TextEditor::textColourId, kText);
  remoteUrlEdit_.setColour(juce::TextEditor::outlineColourId, kGlassStroke);
  remoteUrlEdit_.setColour(juce::TextEditor::focusedOutlineColourId, kAccent.withAlpha(0.55f));
  remoteUrlEdit_.setFont(brandFont_);
  remoteUrlEdit_.setJustification(juce::Justification::centredLeft);
  remoteUrlEdit_.setTextToShowWhenEmpty("ws://host:5000", kMuted);
  remoteUrlEdit_.setText(processor_.getRemoteUrl(), false);
  remoteUrlEdit_.addListener(this);
  remoteButton_.setButtonText(processor_.isRemoteEnabled() ? "cortar" : "vincular");
  serialPanel_.addAndMakeVisible(remoteUrlEdit_);
  serialPanel_.addAndMakeVisible(portBox_);
  serialPanel_.addAndMakeVisible(baudBox_);
  serialPanel_.addAndMakeVisible(midiOutBox_);
  serialPanel_.addAndMakeVisible(inputLowEdit_);
  serialPanel_.addAndMakeVisible(inputHighEdit_);
  serialPanel_.addAndMakeVisible(destList_);
  baudBox_.addItemList({"9600", "19200", "38400", "57600", "115200"}, 1);
  baudBox_.setSelectedId(1, juce::dontSendNotification);
  midiOutBox_.addListener(this);
  setupCompactEdit(inputLowEdit_, "lo");
  setupCompactEdit(inputHighEdit_, "hi");
  inputLowEdit_.setText("4", false);
  inputHighEdit_.setText("16", false);

  debugFilter_.setTextToShowWhenEmpty(U8("ej. chord.spread, px.freq_shift"), kMuted);
  debugFilter_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0x44000000));
  debugFilter_.setColour(juce::TextEditor::textColourId, kText);
  debugFilter_.setColour(juce::TextEditor::outlineColourId, juce::Colour(0x33ffffff));
  debugFilter_.setFont(brandFont_);
  debugFilter_.addListener(this);
  serialPanel_.addAndMakeVisible(debugFilter_);
  debugConsole_.setMultiLine(true, true);
  debugConsole_.setReadOnly(true);
  debugConsole_.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.5f,
                                          juce::Font::plain));
  debugConsole_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0x99080a10));
  debugConsole_.setColour(juce::TextEditor::textColourId, juce::Colour(0xffd8ffe0));
  debugConsole_.setColour(juce::TextEditor::outlineColourId, juce::Colour(0x33ffffff));
  debugConsole_.setVisible(processor_.isSerialDebugConsole());
  serialPanel_.addAndMakeVisible(debugConsole_);

  for (auto* l : {&waveLabel_, &quantLabel_, &droneLabel_}) {
    styleLabel(*l, brandFont_);
    synthPanel_.addAndMakeVisible(l);
  }
  synthPanel_.addAndMakeVisible(waveBox_);
  synthPanel_.addAndMakeVisible(quantBox_);
  synthPanel_.addAndMakeVisible(waveGraph_);
  synthPanel_.addAndMakeVisible(droneSlider_);
  droneReadout_.setFont(brandFont_.withHeight(11.0f));
  droneReadout_.setColour(juce::Label::textColourId, kMuted);
  synthPanel_.addAndMakeVisible(droneReadout_);
  waveBox_.addItemList({"sine", "saw", "square", "triangle"}, 1);
  waveBox_.setSelectedId(1, juce::dontSendNotification);
  waveBox_.addListener(this);
  quantBox_.addItem("off", 1);
  int qid = 2;
  for (size_t i = 0; i < glow::kScaleCount; ++i) quantBox_.addItem(glow::kScales[i].name, qid++);
  quantBox_.addItem("harmonics", qid++);
  quantBox_.setSelectedId(1, juce::dontSendNotification);
  quantBox_.addListener(this);
  setupCompactSlider(droneSlider_, 0, 127, 1);
  droneSlider_.setNumDecimalPlacesToDisplay(0);
  droneSlider_.setValue(processor_.getDroneNote(), juce::dontSendNotification);
  droneSlider_.addListener(this);

  makeParamSliders(kSynthParams, sizeof(kSynthParams) / sizeof(kSynthParams[0]), synthSliders_);
  for (auto& row : synthSliders_) {
    synthPanel_.addAndMakeVisible(row->label);
    synthPanel_.addAndMakeVisible(row->slider);
  }
  makeParamSliders(kModParams, sizeof(kModParams) / sizeof(kModParams[0]), modSliders_);
  for (auto& row : modSliders_) {
    modPanel_.addAndMakeVisible(row->label);
    modPanel_.addAndMakeVisible(row->slider);
  }

  for (auto* l : {&routeTitle_, &granRouteLabel_, &granLevelLabel_, &granBalLabel_, &pxRouteLabel_,
                  &pxLevelLabel_, &pxBalLabel_, &masterLabel_, &balanceLabel_, &gainLLabel_,
                  &gainRLabel_}) {
    styleLabel(*l, brandFont_);
    outPanel_.addAndMakeVisible(l);
  }
  routeTitle_.setFont(brandFont_.withHeight(12.0f));
  routeTitle_.setColour(juce::Label::textColourId, kMuted);
  masterLabel_.setFont(brandFont_.withHeight(12.0f));
  masterLabel_.setColour(juce::Label::textColourId, kMuted);
  balanceHint_.setFont(brandFont_.withHeight(10.5f));
  balanceHint_.setColour(juce::Label::textColourId, kMuted);
  outPanel_.addAndMakeVisible(balanceHint_);
  for (auto* s : {&granLevelSlider_, &granBalSlider_, &pxLevelSlider_, &pxBalSlider_,
                  &balanceSlider_, &gainLSlider_, &gainRSlider_})
    outPanel_.addAndMakeVisible(s);
  outPanel_.addAndMakeVisible(swapOutToggle_);
  outPanel_.addAndMakeVisible(monoSumToggle_);
  outPanel_.addAndMakeVisible(soloLButton_);
  outPanel_.addAndMakeVisible(soloRButton_);
  outPanel_.addAndMakeVisible(bothOutButton_);

  setupCompactSlider(granLevelSlider_, 0.0, 1.0, 0.001);
  setupCompactSlider(granBalSlider_, -1.0, 1.0, 0.001);
  setupCompactSlider(pxLevelSlider_, 0.0, 1.0, 0.001);
  setupCompactSlider(pxBalSlider_, -1.0, 1.0, 0.001);
  setupCompactSlider(balanceSlider_, -1.0, 1.0, 0.001);
  setupCompactSlider(gainLSlider_, 0.0, 1.0, 0.001);
  setupCompactSlider(gainRSlider_, 0.0, 1.0, 0.001);

  granLevelAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
      processor_.apvts(), "route.gran.level", granLevelSlider_);
  granBalAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
      processor_.apvts(), "route.gran.balance", granBalSlider_);
  pxLevelAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
      processor_.apvts(), "route.px.level", pxLevelSlider_);
  pxBalAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
      processor_.apvts(), "route.px.balance", pxBalSlider_);
  balanceAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
      processor_.apvts(), "out.balance", balanceSlider_);
  gainLAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
      processor_.apvts(), "out.gainL", gainLSlider_);
  gainRAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
      processor_.apvts(), "out.gainR", gainRSlider_);
  swapAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
      processor_.apvts(), "out.swap", swapOutToggle_);
  monoAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
      processor_.apvts(), "out.monoSum", monoSumToggle_);
  for (auto* b : {&soloLButton_, &soloRButton_, &bothOutButton_}) b->addListener(this);

  styleCuteControls();
  applyBrandFont(*this);
  loadConfigIntoUi(processor_.getSerialConfig());
  refreshSerialPorts();
  refreshMidiOutputs();
  refreshEngineUi();
  if (pxPanel_ != nullptr) {
    pxPanel_->setBrandFont(brandFont_);
    pxPanel_->refreshFromEngine();
  }
  currentPresetName_ = umbral_presets::kFactoryAntenaGranular;
  refreshPresetList();
  engineButton_.setButtonText(processor_.isEngineOn() ? "stop" : "start");
  setStatus(processor_.getSerialStatus());
  updateDroneReadout();
  updateParamLockUi();
  updateWaveGraph();
  startTimerHz(15);
}

UmbralAudioProcessorEditor::~UmbralAudioProcessorEditor() {
  stopTimer();
  if (pxPanel_ != nullptr) pxPanel_->setLookAndFeel(nullptr);
  setLookAndFeel(nullptr);
  presetBox_.removeListener(this);
  midiOutBox_.removeListener(this);
  waveBox_.removeListener(this);
  quantBox_.removeListener(this);
  granularToggle_.removeListener(this);
  pxToggle_.removeListener(this);
  debugFilter_.removeListener(this);
  savePresetButton_.removeListener(this);
  engineButton_.removeListener(this);
  for (auto* b : {&soloLButton_, &soloRButton_, &bothOutButton_}) b->removeListener(this);
  for (auto* b : {&applyButton_, &connectButton_, &disconnectButton_, &refreshPortsButton_,
                  &addDestButton_})
    b->removeListener(this);
  debugToggle_.removeListener(this);
  remoteButton_.removeListener(this);
  remoteUrlEdit_.removeListener(this);
  droneSlider_.removeListener(this);
}

void UmbralAudioProcessorEditor::applyBrandFont(juce::Component& root) {
  if (&root == &titleLabel_) {
    titleLabel_.setFont(titleFont_);
  } else if (auto* label = dynamic_cast<juce::Label*>(&root)) {
    label->setFont(brandFont_.withHeight(label->getFont().getHeight()));
  }
  for (int i = 0; i < root.getNumChildComponents(); ++i)
    if (auto* c = root.getChildComponent(i)) applyBrandFont(*c);
}

void UmbralAudioProcessorEditor::styleCuteControls() {
  auto tintButton = [](juce::Button& b) {
    b.setColour(juce::TextButton::buttonColourId, juce::Colour(0x14ffffff));
    b.setColour(juce::TextButton::buttonOnColourId, kAccent.withAlpha(0.28f));
    b.setColour(juce::TextButton::textColourOffId, kText);
    b.setColour(juce::TextButton::textColourOnId, kText);
    b.setColour(juce::ToggleButton::textColourId, kText);
    b.setColour(juce::ToggleButton::tickColourId, kAccent);
  };
  for (auto* b : {static_cast<juce::Button*>(&savePresetButton_),
                  static_cast<juce::Button*>(&engineButton_),
                  static_cast<juce::Button*>(&applyButton_),
                  static_cast<juce::Button*>(&connectButton_),
                  static_cast<juce::Button*>(&disconnectButton_),
                  static_cast<juce::Button*>(&refreshPortsButton_),
                  static_cast<juce::Button*>(&addDestButton_),
                  static_cast<juce::Button*>(&soloLButton_),
                  static_cast<juce::Button*>(&soloRButton_),
                  static_cast<juce::Button*>(&bothOutButton_),
                  static_cast<juce::Button*>(&granularToggle_),
                  static_cast<juce::Button*>(&pxToggle_),
                  static_cast<juce::Button*>(&debugToggle_),
                  static_cast<juce::Button*>(&remoteButton_),
                  static_cast<juce::Button*>(&swapOutToggle_),
                  static_cast<juce::Button*>(&monoSumToggle_)})
    tintButton(*b);

  auto tintBox = [](juce::ComboBox& c) {
    c.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0x14ffffff));
    c.setColour(juce::ComboBox::outlineColourId, kGlassStroke);
    c.setColour(juce::ComboBox::textColourId, kText);
    c.setColour(juce::ComboBox::arrowColourId, kMuted);
  };
  for (auto* c : {&presetBox_, &portBox_, &baudBox_, &midiOutBox_, &waveBox_, &quantBox_})
    tintBox(*c);
}

void UmbralAudioProcessorEditor::parentHierarchyChanged() {
  // Hide the standalone window title text ("RUMOR"); keep Options / window buttons.
  if (auto* dw = findParentComponentOfClass<juce::DocumentWindow>())
    if (dw->getName().isNotEmpty())
      dw->setName({});
}

void UmbralAudioProcessorEditor::paint(juce::Graphics& g) {
  if (background_.isValid()) {
    g.drawImage(background_, getLocalBounds().toFloat(),
                juce::RectanglePlacement::fillDestination);
    g.setColour(kBg.withAlpha(0.42f));
    g.fillAll();
  } else {
    g.fillAll(kBg);
  }
}

void UmbralAudioProcessorEditor::layoutSintePage() {
  auto area = sintePage_.getLocalBounds();
  if (area.isEmpty()) return;
  const int synthH = juce::jmax(280, area.getHeight() * 62 / 100);
  synthPanel_.setBounds(area.removeFromTop(synthH));
  area.removeFromTop(kGap);
  modPanel_.setBounds(area);
}

void UmbralAudioProcessorEditor::updateWaveGraph() {
  float wave = 0.0f;
  float timbre = 0.0f;
  if (auto* p = processor_.apvts().getRawParameterValue("osc.waveform")) wave = p->load();
  if (auto* p = processor_.apvts().getRawParameterValue("osc.timbre")) timbre = p->load();
  waveGraph_.setShape(wave, timbre);
}

void UmbralAudioProcessorEditor::resized() {
  auto area = getLocalBounds().reduced(14, 10);

  // Single 40px header: RUMOR centered, utilities on the sides.
  auto header = area.removeFromTop(kHeaderH);
  titleFont_ = titleFont_.withHeight(static_cast<float>(kHeaderH) * 0.9f);
  titleLabel_.setFont(titleFont_);
  titleLabel_.setBounds(header);
  titleLabel_.toBack();

  auto leftHdr = header.removeFromLeft(230);
  auto rightHdr = header.removeFromRight(230);
  presetBox_.setBounds(leftHdr.removeFromLeft(150).reduced(0, 6));
  leftHdr.removeFromLeft(6);
  savePresetButton_.setBounds(leftHdr.removeFromLeft(72).reduced(0, 6));
  engineButton_.setBounds(rightHdr.removeFromRight(72).reduced(0, 6));
  statusLabel_.setBounds(rightHdr.reduced(4, 6));
  subtitleLabel_.setBounds({});
  presetBox_.toFront(false);
  savePresetButton_.toFront(false);
  engineButton_.toFront(false);
  statusLabel_.toFront(false);

  area.removeFromTop(kGap);

  // Footer: engine enable checkboxes.
  auto footer = area.removeFromBottom(kFooterH);
  area.removeFromBottom(4);
  granularToggle_.setBounds(footer.removeFromLeft(80).reduced(0, 6));
  footer.removeFromLeft(8);
  pxToggle_.setBounds(footer.removeFromLeft(120).reduced(0, 6));

  // Modules take the remaining vertical space.
  const int leftW = area.getWidth() * 48 / 100;
  leftModule_.setBounds(area.removeFromLeft(leftW));
  area.removeFromLeft(kGap);
  rightModule_.setBounds(area);
  leftTabs_.setBounds(leftModule_.contentBounds());
  rightTabs_.setBounds(rightModule_.contentBounds());
  layoutSintePage();

  // Serial internals
  {
    auto serial = serialPanel_.contentBounds();
    auto sRemote = serial.removeFromTop(kRowH);
    remoteLabel_.setBounds(sRemote.removeFromLeft(52));
    remoteButton_.setBounds(sRemote.removeFromRight(72));
    sRemote.removeFromRight(4);
    remoteUrlEdit_.setBounds(sRemote);
    serial.removeFromTop(4);
    auto s1 = serial.removeFromTop(kRowH);
    portLabel_.setBounds(s1.removeFromLeft(48));
    portBox_.setBounds(s1.removeFromLeft(s1.getWidth() - 170));
    s1.removeFromLeft(4);
    refreshPortsButton_.setBounds(s1.removeFromLeft(80));
    s1.removeFromLeft(4);
    baudLabel_.setBounds(s1.removeFromLeft(36));
    baudBox_.setBounds(s1);
    serial.removeFromTop(4);
    auto sMidi = serial.removeFromTop(kRowH);
    midiOutLabel_.setBounds(sMidi.removeFromLeft(64));
    midiOutBox_.setBounds(sMidi);
    serial.removeFromTop(4);
    auto s2 = serial.removeFromTop(kRowH);
    inputLabel_.setBounds(s2.removeFromLeft(52));
    inputLowEdit_.setBounds(s2.removeFromLeft(64));
    s2.removeFromLeft(4);
    inputHighEdit_.setBounds(s2.removeFromLeft(64));
    s2.removeFromLeft(8);
    debugToggle_.setBounds(s2.removeFromLeft(70));
    serial.removeFromTop(4);
    auto s3 = serial.removeFromTop(kRowH);
    destSectionLabel_.setBounds(s3.removeFromLeft(64));
    addDestButton_.setBounds(s3.removeFromLeft(70));
    s3.removeFromLeft(6);
    applyButton_.setBounds(s3.removeFromLeft(70));
    s3.removeFromLeft(4);
    connectButton_.setBounds(s3.removeFromLeft(80));
    s3.removeFromLeft(4);
    disconnectButton_.setBounds(s3.removeFromLeft(96));
    serial.removeFromTop(4);
    const int debugH = debugConsole_.isVisible() ? 150 : 0;
    if (debugH > 0) {
      auto dbg = serial.removeFromBottom(debugH);
      auto filt = dbg.removeFromTop(24);
      filterLabel_.setBounds(filt.removeFromLeft(48));
      debugFilter_.setBounds(filt);
      dbg.removeFromTop(4);
      debugConsole_.setBounds(dbg);
      serial.removeFromBottom(4);
    } else {
      filterLabel_.setBounds({});
      debugFilter_.setBounds({});
      debugConsole_.setBounds({});
    }
    destList_.setBounds(serial);
    layoutDestinationRows();
  }

  // Synth
  {
    auto synth = synthPanel_.contentBounds();
    auto r1 = synth.removeFromTop(kRowH);
    waveLabel_.setBounds(r1.removeFromLeft(40));
    waveBox_.setBounds(r1.removeFromLeft(100));
    r1.removeFromLeft(6);
    quantLabel_.setBounds(r1.removeFromLeft(40));
    quantBox_.setBounds(r1);
    synth.removeFromTop(4);
    waveGraph_.setBounds(synth.removeFromTop(56));
    synth.removeFromTop(4);
    auto r2 = synth.removeFromTop(kRowH);
    droneLabel_.setBounds(r2.removeFromLeft(48));
    droneSlider_.setBounds(r2.removeFromLeft(juce::jmax(80, r2.getWidth() - 120)));
    r2.removeFromLeft(4);
    droneReadout_.setBounds(r2);
    synth.removeFromTop(6);
    for (auto& row : synthSliders_) {
      auto rr = synth.removeFromTop(kRowH);
      row->label.setBounds(rr.removeFromLeft(100));
      row->slider.setBounds(rr);
      synth.removeFromTop(2);
    }
  }

  // Mod
  {
    auto m = modPanel_.contentBounds();
    for (auto& row : modSliders_) {
      auto rr = m.removeFromTop(kRowH);
      row->label.setBounds(rr.removeFromLeft(100));
      row->slider.setBounds(rr);
      m.removeFromTop(2);
    }
  }

  // Salida / routing
  {
    auto out = outPanel_.contentBounds();
    routeTitle_.setBounds(out.removeFromTop(20));
    out.removeFromTop(2);
    granRouteLabel_.setBounds(out.removeFromTop(18));
    auto g1 = out.removeFromTop(kRowH);
    granLevelLabel_.setBounds(g1.removeFromLeft(48));
    granLevelSlider_.setBounds(g1);
    out.removeFromTop(2);
    auto g2 = out.removeFromTop(kRowH);
    granBalLabel_.setBounds(g2.removeFromLeft(88));
    granBalSlider_.setBounds(g2);
    out.removeFromTop(6);
    pxRouteLabel_.setBounds(out.removeFromTop(18));
    auto p1 = out.removeFromTop(kRowH);
    pxLevelLabel_.setBounds(p1.removeFromLeft(48));
    pxLevelSlider_.setBounds(p1);
    out.removeFromTop(2);
    auto p2 = out.removeFromTop(kRowH);
    pxBalLabel_.setBounds(p2.removeFromLeft(88));
    pxBalSlider_.setBounds(p2);
    out.removeFromTop(8);
    masterLabel_.setBounds(out.removeFromTop(20));
    out.removeFromTop(2);
    auto o1 = out.removeFromTop(kRowH);
    balanceLabel_.setBounds(o1.removeFromLeft(56));
    balanceSlider_.setBounds(o1.removeFromLeft(juce::jmax(100, o1.getWidth() - 140)));
    o1.removeFromLeft(4);
    balanceHint_.setBounds(o1);
    out.removeFromTop(2);
    auto o2 = out.removeFromTop(kRowH);
    gainLLabel_.setBounds(o2.removeFromLeft(52));
    gainLSlider_.setBounds(o2.removeFromLeft(o2.getWidth() / 2 - 8));
    o2.removeFromLeft(10);
    gainRLabel_.setBounds(o2.removeFromLeft(64));
    gainRSlider_.setBounds(o2);
    out.removeFromTop(6);
    auto o3 = out.removeFromTop(kRowH);
    monoSumToggle_.setBounds(o3.removeFromLeft(96));
    o3.removeFromLeft(4);
    swapOutToggle_.setBounds(o3.removeFromLeft(84));
    o3.removeFromLeft(8);
    soloLButton_.setBounds(o3.removeFromLeft(64));
    o3.removeFromLeft(4);
    soloRButton_.setBounds(o3.removeFromLeft(64));
    o3.removeFromLeft(4);
    bothOutButton_.setBounds(o3.removeFromLeft(64));
  }
}

void UmbralAudioProcessorEditor::layoutDestinationRows() {
  int y = 0;
  const int w = destList_.getWidth();
  for (auto& row : destRows_) {
    row->setBounds(0, y, w, kDestRowH);
    y += kDestRowH + 2;
  }
}

void UmbralAudioProcessorEditor::addDestinationRow(const glow::SerialRoute& route) {
  if (static_cast<int>(destRows_.size()) >= glow::kSerialMaxDestinations) return;
  auto row = std::make_unique<DestinationRow>();
  int idx = 0;
  for (int i = 0; i < kDestNameCount; ++i) {
    glow::SerialDestination d{};
    if (glow::serialDestinationFromName(kDestNames[i], d) && d == route.destination) {
      idx = i;
      break;
    }
  }
  row->parameterBox.setSelectedItemIndex(idx, juce::dontSendNotification);
  const bool isCc = route.destination == glow::SerialDestination::MidiCc;
  row->ccLabel.setVisible(isCc);
  row->ccSlider.setVisible(isCc);
  row->ccSlider.setValue(route.midiCc, juce::dontSendNotification);
  row->minEdit.setText(juce::String(route.min, 4), false);
  row->maxEdit.setText(juce::String(route.max, 4), false);
  row->invertToggle.setToggleState(route.invert, juce::dontSendNotification);
  row->onRemove = [this](DestinationRow* victim) {
    destList_.removeChildComponent(victim);
    destRows_.erase(std::remove_if(destRows_.begin(), destRows_.end(),
                                   [victim](const auto& p) { return p.get() == victim; }),
                    destRows_.end());
    layoutDestinationRows();
  };
  destList_.addAndMakeVisible(*row);
  destRows_.push_back(std::move(row));
  layoutDestinationRows();
}

void UmbralAudioProcessorEditor::loadConfigIntoUi(const glow::SerialConfig& cfg) {
  syncingUi_ = true;
  const int baudIds[] = {9600, 19200, 38400, 57600, 115200};
  baudBox_.setSelectedItemIndex(0, juce::dontSendNotification);
  for (int i = 0; i < 5; ++i)
    if (baudIds[i] == cfg.baud) {
      baudBox_.setSelectedItemIndex(i, juce::dontSendNotification);
      break;
    }
  if (cfg.droneNote >= 0) droneSlider_.setValue(cfg.droneNote, juce::dontSendNotification);
  waveBox_.setSelectedItemIndex(0, juce::dontSendNotification);
  if (cfg.waveform)
    for (int i = 0; i < waveBox_.getNumItems(); ++i)
      if (waveBox_.getItemText(i) == juce::String(*cfg.waveform)) {
        waveBox_.setSelectedItemIndex(i, juce::dontSendNotification);
        break;
      }
  quantBox_.setSelectedItemIndex(0, juce::dontSendNotification);
  if (cfg.quantize)
    for (int i = 0; i < quantBox_.getNumItems(); ++i)
      if (quantBox_.getItemText(i) == juce::String(*cfg.quantize)) {
        quantBox_.setSelectedItemIndex(i, juce::dontSendNotification);
        break;
      }
  inputLowEdit_.setText(juce::String(cfg.inputLow), false);
  inputHighEdit_.setText(juce::String(cfg.inputHigh), false);
  destRows_.clear();
  destList_.removeAllChildren();
  for (const auto& route : cfg.destinations) addDestinationRow(route);
  syncingUi_ = false;
  resized();
}

glow::SerialConfig UmbralAudioProcessorEditor::buildConfigFromUi() const {
  glow::SerialConfig cfg;
  const int portIndex = portBox_.getSelectedItemIndex();
  if (portIndex > 0 && portIndex < portPaths_.size())
    cfg.port = portPaths_[portIndex].toStdString();
  const int baudIds[] = {9600, 19200, 38400, 57600, 115200};
  cfg.baud = baudIds[juce::jlimit(0, 4, baudBox_.getSelectedItemIndex())];
  cfg.droneNote = static_cast<int>(droneSlider_.getValue());
  cfg.waveform = waveBox_.getText().toStdString();
  cfg.quantize = quantBox_.getText().toStdString();
  if (*cfg.quantize == "off") cfg.quantize.reset();
  cfg.inputLow = inputLowEdit_.getText().trim().getIntValue();
  cfg.inputHigh = inputHighEdit_.getText().trim().getIntValue();
  cfg.debug = debugToggle_.getToggleState();
  for (const auto& row : destRows_) {
    glow::SerialDestination dest = glow::SerialDestination::Pitch;
    glow::serialDestinationFromName(row->parameterBox.getText().toStdString(), dest);
    glow::SerialRoute route;
    route.destination = dest;
    route.min = row->minEdit.getText().trim().getFloatValue();
    route.max = row->maxEdit.getText().trim().getFloatValue();
    route.invert = row->invertToggle.getToggleState();
    route.midiCc = juce::jlimit(0, 127, juce::roundToInt(row->ccSlider.getValue()));
    cfg.destinations.push_back(route);
  }
  return cfg;
}

void UmbralAudioProcessorEditor::refreshSerialPorts() {
  const auto selected = portBox_.getText();
  const auto cfgPort = juce::String(processor_.getSerialConfig().port);
  portBox_.clear(juce::dontSendNotification);
  portPaths_.clear();
  portBox_.addItem(U8("(ninguno)"), 1);
  portPaths_.add({});
  int id = 2;
  for (const auto& path : processor_.listSerialPorts()) {
    portBox_.addItem(path, id++);
    portPaths_.add(path);
  }
  int match = 0;
  for (int i = 0; i < portPaths_.size(); ++i)
    if (portPaths_[i] == selected || portPaths_[i] == cfgPort) {
      match = i;
      if (portPaths_[i] == selected) break;
    }
  portBox_.setSelectedItemIndex(match, juce::dontSendNotification);
}

void UmbralAudioProcessorEditor::refreshMidiOutputs() {
  ignoreMidiOutChange_ = true;
  const auto currentId = processor_.getMidiOutputIdentifier();
  midiOutBox_.clear(juce::dontSendNotification);
  midiOutIds_.clear();
  midiOutBox_.addItem(U8("(solo host)"), 1);
  midiOutIds_.add({});
  int id = 2, match = 0;
  for (const auto& d : processor_.listMidiOutputDevices()) {
    midiOutBox_.addItem(d.name, id++);
    midiOutIds_.add(d.identifier);
    if (d.identifier == currentId) match = midiOutIds_.size() - 1;
  }
  if (currentId.isNotEmpty() && match == 0) {
    const auto name = processor_.getMidiOutputName();
    midiOutBox_.addItem(name.isNotEmpty() ? name + " (offline)" : "(offline)", id);
    midiOutIds_.add(currentId);
    match = midiOutIds_.size() - 1;
  }
  midiOutBox_.setSelectedItemIndex(match, juce::dontSendNotification);
  ignoreMidiOutChange_ = false;
}

void UmbralAudioProcessorEditor::setStatus(const juce::String& text, bool isError) {
  statusLabel_.setColour(juce::Label::textColourId, isError ? juce::Colours::orange : kMuted);
  statusLabel_.setText(text, juce::dontSendNotification);
}

void UmbralAudioProcessorEditor::updateDroneReadout() {
  const float midi = processor_.getDisplayedPitchMidi();
  const int midiInt = juce::roundToInt(midi);
  const float hz = processor_.getSoundingFrequencyHz();
  juce::String text;
  if (std::abs(midi - static_cast<float>(midiInt)) < 0.05f)
    text << "MIDI " << midiInt;
  else
    text << "MIDI " << juce::String(midi, 2);
  text << "  |  " << juce::String(hz, 1) << " Hz";
  if (processor_.isSerialPitchLive()) text << "  (serial)";
  droneReadout_.setText(text, juce::dontSendNotification);
}

void UmbralAudioProcessorEditor::updateParamLockUi() {
  const bool pitchLocked = processor_.isSerialPitchLive();
  droneSlider_.setEnabled(!pitchLocked);
  droneSlider_.setAlpha(pitchLocked ? 0.55f : 1.0f);
  for (auto& row : synthSliders_) {
    const bool locked = processor_.isSerialConnected() && processor_.isSerialDriving(row->id);
    row->slider.setEnabled(!locked);
    row->slider.setAlpha(locked ? 0.55f : 1.0f);
  }
  for (auto& row : modSliders_) {
    const bool locked = processor_.isSerialConnected() && processor_.isSerialDriving(row->id);
    row->slider.setEnabled(!locked);
    row->slider.setAlpha(locked ? 0.55f : 1.0f);
  }
}

void UmbralAudioProcessorEditor::syncSlidersFromEngine() {
  syncingUi_ = true;
  auto sync = [this](std::vector<std::unique_ptr<ParamSlider>>& list) {
    for (auto& row : list) {
      if (!processor_.isSerialDriving(row->id)) continue;
      row->slider.setValue(processor_.getParameterTarget(row->id), juce::dontSendNotification);
    }
  };
  sync(synthSliders_);
  sync(modSliders_);
  if (processor_.isSerialPitchLive())
    droneSlider_.setValue(processor_.getDisplayedPitchMidi(), juce::dontSendNotification);
  else
    droneSlider_.setValue(processor_.getDroneNote(), juce::dontSendNotification);
  syncingUi_ = false;
  updateDroneReadout();
  updateParamLockUi();
}

void UmbralAudioProcessorEditor::appendDebugChunk(const juce::String& chunk) {
  if (chunk.isEmpty()) return;
  juce::StringArray lines;
  lines.addLines(chunk);
  for (auto& line : lines)
    if (line.isNotEmpty()) debugHistory_.add(line);
  while (debugHistory_.size() > 400) debugHistory_.remove(0);
  rebuildDebugView();
}

void UmbralAudioProcessorEditor::rebuildDebugView() {
  // Comma-separated key filters (e.g. "chord.spread, px.freq_shift") keep only matching key=value
  // tokens on each line. Empty filter shows the full line.
  juce::StringArray terms;
  for (const auto& t : juce::StringArray::fromTokens(debugFilter_.getText(), ",", "")) {
    const auto trimmed = t.trim();
    if (trimmed.isNotEmpty()) terms.add(trimmed);
  }

  juce::String out;
  for (const auto& line : debugHistory_) {
    if (terms.isEmpty()) {
      out << line << "\n";
      continue;
    }

    juce::StringArray kept;
    for (const auto& token : juce::StringArray::fromTokens(line, " \t", "")) {
      if (token.isEmpty()) continue;
      const int eq = token.indexOfChar('=');
      if (eq <= 0) continue;
      const auto key = token.substring(0, eq);
      bool match = false;
      for (const auto& term : terms) {
        if (key.containsIgnoreCase(term) || term.containsIgnoreCase(key)) {
          match = true;
          break;
        }
      }
      if (match) kept.add(token);
    }
    if (kept.isEmpty()) continue;
    out << kept.joinIntoString(" ") << "\n";
  }
  debugConsole_.setText(out, false);
  debugConsole_.moveCaretToEnd();
}

void UmbralAudioProcessorEditor::textEditorTextChanged(juce::TextEditor& ed) {
  if (&ed == &debugFilter_) rebuildDebugView();
}

void UmbralAudioProcessorEditor::textEditorReturnKeyPressed(juce::TextEditor& ed) {
  if (&ed != &remoteUrlEdit_) return;
  const auto url = remoteUrlEdit_.getText().trim();
  if (url.isEmpty()) {
    setStatus("URL vacia.", true);
    return;
  }
  processor_.connectRemote(url);
}

void UmbralAudioProcessorEditor::timerCallback() {
  engineButton_.setButtonText(processor_.isEngineOn() ? "stop" : "start");
  remoteButton_.setButtonText(processor_.isRemoteEnabled() ? "cortar" : "vincular");
  remoteButton_.setColour(juce::TextButton::buttonColourId,
                          processor_.isRemoteConnected() ? kAccent.withAlpha(0.28f)
                                                         : juce::Colour(0x14ffffff));

  juce::String status;
  if (processor_.isSerialConnected()) status = processor_.getSerialStatus();
  const auto remote = processor_.getRemoteStatus();
  if (remote.isNotEmpty()) {
    if (status.isNotEmpty()) status << "  |  ";
    status << remote;
  }
  if (status.isNotEmpty()) setStatus(status);

  const auto uiRev = processor_.remoteUiRevision();
  if (uiRev != lastRemoteUiRevision_) {
    lastRemoteUiRevision_ = uiRev;
    loadConfigIntoUi(processor_.getSerialConfig());
    refreshEngineUi();
    const bool debugOn = processor_.isSerialDebugConsole();
    debugToggle_.setToggleState(debugOn, juce::dontSendNotification);
    debugConsole_.setVisible(debugOn);
    debugFilter_.setVisible(debugOn);
    filterLabel_.setVisible(debugOn);
    resized();
    updateDroneReadout();
    updateParamLockUi();
  }
  const auto pxRev = processor_.remotePxRevision();
  if (pxRev != lastRemotePxRevision_) {
    lastRemotePxRevision_ = pxRev;
    if (pxPanel_ != nullptr) pxPanel_->refreshFromEngine();
  }

  syncSlidersFromEngine();
  updateWaveGraph();
  if (debugConsole_.isVisible()) appendDebugChunk(processor_.consumeDebugLog());
}

void UmbralAudioProcessorEditor::sliderValueChanged(juce::Slider* slider) {
  if (syncingUi_ || slider != &droneSlider_) return;
  if (processor_.isSerialPitchLive()) return;
  processor_.setDroneNote(juce::roundToInt(droneSlider_.getValue()));
  updateDroneReadout();
}

bool UmbralAudioProcessorEditor::applyFromUi(bool connectSerial) {
  juce::String status;
  if (!processor_.applyConfig(buildConfigFromUi(), status, connectSerial)) {
    setStatus(status, true);
    return false;
  }
  updateParamLockUi();
  setStatus(status);
  return true;
}

void UmbralAudioProcessorEditor::refreshPresetList() {
  ignorePresetChange_ = true;
  const auto names = umbral_presets::listPresetNames();
  presetBox_.clear(juce::dontSendNotification);
  int selectedId = 1;
  for (int i = 0; i < names.size(); ++i) {
    const int id = i + 1;
    presetBox_.addItem(names[i], id);
    if (names[i] == currentPresetName_) selectedId = id;
  }
  presetBox_.setSelectedId(selectedId, juce::dontSendNotification);
  ignorePresetChange_ = false;
}

void UmbralAudioProcessorEditor::loadSelectedPreset() {
  const auto name = presetBox_.getText();
  if (name.isEmpty()) return;
  juce::String err;
  auto preset = umbral_presets::loadPreset(name, err);
  if (!preset) {
    setStatus(err, true);
    return;
  }
  juce::String status;
  if (!umbral_presets::applyPreset(processor_, *preset, status)) {
    setStatus(status, true);
    return;
  }
  currentPresetName_ = name;
  loadConfigIntoUi(processor_.getSerialConfig());
  refreshEngineUi();
  setStatus(status);
}

void UmbralAudioProcessorEditor::saveCurrentAsPreset() {
  juce::String applyStatus;
  processor_.applyConfig(buildConfigFromUi(), applyStatus, false);
  auto* aw = new juce::AlertWindow(U8("Guardar preset"),
                                   U8("Nombre del preset (el puerto serial no se guarda):"),
                                   juce::MessageBoxIconType::QuestionIcon);
  aw->addTextEditor("name", currentPresetName_.isNotEmpty() ? currentPresetName_ : "mi preset",
                    U8("Nombre"));
  aw->addButton(U8("Guardar"), 1, juce::KeyPress(juce::KeyPress::returnKey));
  aw->addButton(U8("Cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
  aw->enterModalState(true, juce::ModalCallbackFunction::create([this, aw](int result) {
                        const juce::String name = aw->getTextEditorContents("name").trim();
                        if (result != 1 || name.isEmpty()) return;
                        const auto preset = umbral_presets::capturePreset(processor_, name);
                        juce::String err;
                        if (!umbral_presets::savePreset(name, preset, err)) {
                          setStatus(err, true);
                          return;
                        }
                        currentPresetName_ = name;
                        refreshPresetList();
                        setStatus(U8("Preset guardado: \"") + name + "\".");
                      }),
                      true);
}

void UmbralAudioProcessorEditor::refreshEngineUi() {
  ignoreEngineChange_ = true;
  granularToggle_.setToggleState(processor_.isGranularEnabled(), juce::dontSendNotification);
  pxToggle_.setToggleState(processor_.isPaulXStretchEnabled(), juce::dontSendNotification);
  pxToggle_.setEnabled(processor_.hasPaulXStretch());
  ignoreEngineChange_ = false;
}

void UmbralAudioProcessorEditor::comboBoxChanged(juce::ComboBox* box) {
  if (box == &waveBox_ || box == &quantBox_) {
    if (syncingUi_) return;
    // Apply wave/scale immediately so synth is editable without serial.
    juce::String status;
    processor_.applyConfig(buildConfigFromUi(), status, false);
    updateWaveGraph();
    return;
  }
  if (box == &midiOutBox_) {
    if (ignoreMidiOutChange_) return;
    const int idx = midiOutBox_.getSelectedItemIndex();
    const juce::String identifier =
        (idx >= 0 && idx < midiOutIds_.size()) ? midiOutIds_[idx] : juce::String();
    juce::String status;
    if (!processor_.setMidiOutputDevice(identifier, status))
      setStatus(status, true);
    else
      setStatus(status);
    return;
  }
  if (ignorePresetChange_ || box != &presetBox_) return;
  loadSelectedPreset();
}

void UmbralAudioProcessorEditor::buttonClicked(juce::Button* button) {
  if (button == &granularToggle_) {
    if (ignoreEngineChange_) return;
    processor_.setGranularEnabled(granularToggle_.getToggleState());
    setStatus(granularToggle_.getToggleState() ? "sinte on" : "sinte off");
    return;
  }
  if (button == &pxToggle_) {
    if (ignoreEngineChange_) return;
    processor_.setPaulXStretchEnabled(pxToggle_.getToggleState());
    if (pxPanel_ != nullptr) pxPanel_->refreshFromEngine();
    setStatus(pxToggle_.getToggleState() ? "paulxstretch on" : "paulxstretch off");
    return;
  }
  if (button == &savePresetButton_) {
    saveCurrentAsPreset();
    return;
  }
  if (button == &soloLButton_) {
    processor_.setOutputBalance(-1.0f);
    setStatus(U8("Solo L (Subwoofer)."));
    return;
  }
  if (button == &soloRButton_) {
    processor_.setOutputBalance(1.0f);
    setStatus(U8("Solo R (Dayton)."));
    return;
  }
  if (button == &bothOutButton_) {
    processor_.setOutputBalance(0.0f);
    processor_.setOutputGainL(1.0f);
    processor_.setOutputGainR(1.0f);
    setStatus(U8("Ambas salidas (centro)."));
    return;
  }
  if (button == &engineButton_) {
    processor_.setEngineOn(!processor_.isEngineOn());
    setStatus(processor_.isEngineOn() ? U8("Engine on.") : U8("Engine off."));
    return;
  }
  if (button == &remoteButton_) {
    if (processor_.isRemoteEnabled()) {
      processor_.disconnectRemote();
      setStatus(U8("Remoto desconectado."));
      return;
    }
    const auto url = remoteUrlEdit_.getText().trim();
    if (url.isEmpty()) {
      setStatus("URL vacia.", true);
      return;
    }
    processor_.connectRemote(url);
    return;
  }
  if (button == &refreshPortsButton_) {
    refreshSerialPorts();
    refreshMidiOutputs();
    setStatus(U8("Puertos actualizados."));
    return;
  }
  if (button == &disconnectButton_) {
    processor_.disconnectSerial();
    updateParamLockUi();
    setStatus(U8("Serial desconectado."));
    return;
  }
  if (button == &addDestButton_) {
    if (static_cast<int>(destRows_.size()) >= glow::kSerialMaxDestinations) {
      setStatus(U8("M\xc3\xa1ximo de destinos alcanzado."), true);
      return;
    }
    addDestinationRow({glow::SerialDestination::ChordSpread, 0.0f, 1.0f, false});
    return;
  }
  if (button == &debugToggle_) {
    const bool on = debugToggle_.getToggleState();
    processor_.setSerialDebugConsole(on);
    debugConsole_.setVisible(on);
    filterLabel_.setVisible(on);
    debugFilter_.setVisible(on);
    if (!on) {
      debugConsole_.clear();
      debugHistory_.clear();
    }
    resized();
    setStatus(on ? U8("Debug on \xe2\x80\x94 filtro ej. chord.spread, px.freq_shift")
                 : U8("Debug off."));
    return;
  }
  if (button == &applyButton_) applyFromUi(false);
  if (button == &connectButton_) applyFromUi(true);
}
