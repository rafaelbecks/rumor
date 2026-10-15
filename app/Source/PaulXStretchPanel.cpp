#include "PaulXStretchPanel.h"

namespace {
constexpr int kRowH = 26;
constexpr int kGap = 6;
constexpr int kWaveH = 88;

juce::String U8(const char* utf8) { return juce::String::fromUTF8(utf8); }

const juce::Colour kGlass{0xe61c1c1f};
const juce::Colour kGlassStroke{0x22ffffff};
const juce::Colour kAccent{0xffc4a0ff};
const juce::Colour kText{0xffe8e8ea};
const juce::Colour kMuted{0xff8a8a92};

void styleLabel(juce::Label& label, const juce::Font& font, float alpha = 1.0f) {
  label.setFont(font);
  label.setColour(juce::Label::textColourId, kText.withAlpha(alpha));
  label.setJustificationType(juce::Justification::centredLeft);
}
}  // namespace

// ---------------------------------------------------------------------------
// SourceWaveform
// ---------------------------------------------------------------------------

PaulXStretchPanel::SourceWaveform::SourceWaveform(UmbralAudioProcessor& processor)
    : processor_(processor) {
  formatManager_.registerBasicFormats();
  thumbnail_.addChangeListener(this);
  setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
}

PaulXStretchPanel::SourceWaveform::~SourceWaveform() {
  thumbnail_.removeChangeListener(this);
  thumbnail_.clear();
}

void PaulXStretchPanel::SourceWaveform::changeListenerCallback(juce::ChangeBroadcaster*) {
  repaint();
}

void PaulXStretchPanel::SourceWaveform::reloadFromProcessor() {
  const auto file = processor_.paulXStretchSourceFile();
  thumbnail_.clear();
  if (file.existsAsFile())
    thumbnail_.setSource(new juce::FileInputSource(file));
  float s = 0.0f, e = 1.0f;
  processor_.getPaulXStretchParameter("px.sound_start", s);
  processor_.getPaulXStretchParameter("px.sound_end", e);
  start_ = juce::jlimit(0.0f, 1.0f, s);
  end_ = juce::jlimit(0.0f, 1.0f, e);
  if (end_ < start_) std::swap(start_, end_);
  repaint();
}

void PaulXStretchPanel::SourceWaveform::setPlayhead(float norm01) {
  const float n = juce::jlimit(0.0f, 1.0f, norm01);
  if (std::abs(n - playhead_) < 0.0005f) return;
  playhead_ = n;
  repaint();
}

void PaulXStretchPanel::SourceWaveform::setRange(float start01, float end01) {
  start_ = juce::jlimit(0.0f, 1.0f, start01);
  end_ = juce::jlimit(0.0f, 1.0f, end01);
  if (end_ < start_) std::swap(start_, end_);
  repaint();
}

float PaulXStretchPanel::SourceWaveform::xToNorm(float x) const {
  const auto r = getLocalBounds().toFloat().reduced(4.0f, 2.0f);
  if (r.getWidth() <= 1.0f) return 0.0f;
  return juce::jlimit(0.0f, 1.0f, (x - r.getX()) / r.getWidth());
}

float PaulXStretchPanel::SourceWaveform::normToX(float n) const {
  const auto r = getLocalBounds().toFloat().reduced(4.0f, 2.0f);
  return r.getX() + juce::jlimit(0.0f, 1.0f, n) * r.getWidth();
}

PaulXStretchPanel::SourceWaveform::DragTarget PaulXStretchPanel::SourceWaveform::hitHandle(
    float x) const {
  const float xs = normToX(start_);
  const float xe = normToX(end_);
  constexpr float kHit = 8.0f;
  if (std::abs(x - xs) <= kHit) return DragTarget::Start;
  if (std::abs(x - xe) <= kHit) return DragTarget::End;
  if (x > xs && x < xe) return DragTarget::Region;
  return DragTarget::None;
}

void PaulXStretchPanel::SourceWaveform::applyRangeToProcessor() {
  processor_.setPaulXStretchParameter("px.sound_start", start_);
  processor_.setPaulXStretchParameter("px.sound_end", end_);
}

void PaulXStretchPanel::SourceWaveform::mouseDown(const juce::MouseEvent& e) {
  drag_ = hitHandle(static_cast<float>(e.x));
  if (drag_ == DragTarget::None) {
    // Click outside region: set nearest edge
    const float n = xToNorm(static_cast<float>(e.x));
    if (std::abs(n - start_) < std::abs(n - end_)) {
      start_ = n;
      drag_ = DragTarget::Start;
    } else {
      end_ = n;
      drag_ = DragTarget::End;
    }
    if (end_ < start_) std::swap(start_, end_);
    applyRangeToProcessor();
    repaint();
    return;
  }
  if (drag_ == DragTarget::Region)
    dragGrabOffset_ = xToNorm(static_cast<float>(e.x)) - start_;
}

void PaulXStretchPanel::SourceWaveform::mouseDrag(const juce::MouseEvent& e) {
  if (drag_ == DragTarget::None) return;
  const float n = xToNorm(static_cast<float>(e.x));
  if (drag_ == DragTarget::Start) {
    start_ = juce::jmin(n, end_ - 0.002f);
  } else if (drag_ == DragTarget::End) {
    end_ = juce::jmax(n, start_ + 0.002f);
  } else if (drag_ == DragTarget::Region) {
    const float len = end_ - start_;
    float newStart = juce::jlimit(0.0f, 1.0f - len, n - dragGrabOffset_);
    start_ = newStart;
    end_ = newStart + len;
  }
  applyRangeToProcessor();
  repaint();
}

void PaulXStretchPanel::SourceWaveform::mouseUp(const juce::MouseEvent&) {
  drag_ = DragTarget::None;
}

void PaulXStretchPanel::SourceWaveform::resized() { repaint(); }

void PaulXStretchPanel::SourceWaveform::paint(juce::Graphics& g) {
  auto bounds = getLocalBounds().toFloat();
  g.setColour(juce::Colour(0x44000000));
  g.fillRoundedRectangle(bounds, 8.0f);
  g.setColour(kGlassStroke);
  g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);

  auto r = bounds.reduced(4.0f, 2.0f);
  if (thumbnail_.getTotalLength() <= 0.0) {
    g.setColour(kMuted);
    g.setFont(12.0f);
    g.drawFittedText(U8("Carga un audio para ver la forma de onda"), r.toNearestInt(),
                     juce::Justification::centred, 1);
    return;
  }

  // Inactive regions
  g.setColour(juce::Colour(0x33000000));
  g.fillRect(r.withWidth(normToX(start_) - r.getX()));
  g.fillRect(juce::Rectangle<float>(normToX(end_), r.getY(), r.getRight() - normToX(end_),
                                    r.getHeight()));

  // Waveform
  g.setColour(kAccent.withAlpha(0.85f));
  thumbnail_.drawChannels(g, r.toNearestInt(), 0.0, thumbnail_.getTotalLength(), 1.0f);

  // Selection tint
  const float x0 = normToX(start_);
  const float x1 = normToX(end_);
  g.setColour(kAccent.withAlpha(0.12f));
  g.fillRect(juce::Rectangle<float>(x0, r.getY(), x1 - x0, r.getHeight()));

  // Start / end handles
  auto drawHandle = [&](float x, const juce::String& tag) {
    g.setColour(kText);
    g.drawLine(x, r.getY(), x, r.getBottom(), 1.5f);
    g.setColour(kAccent);
    g.fillRoundedRectangle(juce::Rectangle<float>(x - 3.0f, r.getY(), 6.0f, r.getHeight()), 2.0f);
    g.setColour(kText.withAlpha(0.8f));
    g.setFont(10.0f);
    g.drawText(tag, juce::Rectangle<float>(x - 10.0f, r.getY() + 2.0f, 20.0f, 12.0f),
               juce::Justification::centred);
  };
  drawHandle(x0, "S");
  drawHandle(x1, "E");

  // Playhead
  const float xp = normToX(playhead_);
  g.setColour(juce::Colours::white.withAlpha(0.85f));
  g.drawLine(xp, r.getY(), xp, r.getBottom(), 1.2f);
}

// ---------------------------------------------------------------------------
// PaulXStretchPanel
// ---------------------------------------------------------------------------

PaulXStretchPanel::PaulXStretchPanel(UmbralAudioProcessor& processor) : processor_(processor) {
  title_.setText("paulxstretch", juce::dontSendNotification);
  title_.setColour(juce::Label::textColourId, kMuted);
  addAndMakeVisible(title_);

  subtitle_.setText("misma instancia · ruteo en salida", juce::dontSendNotification);
  subtitle_.setColour(juce::Label::textColourId, kMuted);
  addAndMakeVisible(subtitle_);

  styleLabel(sourceLabel_, brandFont_, 0.85f);
  styleLabel(playheadLabel_, brandFont_, 0.75f);
  styleLabel(modulesLabel_, brandFont_, 0.9f);
  modulesLabel_.setText("modules", juce::dontSendNotification);
  addAndMakeVisible(sourceLabel_);
  addAndMakeVisible(playheadLabel_);
  addAndMakeVisible(modulesLabel_);

  loadButton_.setButtonText(U8("cargar\xe2\x80\xa6"));
  clearButton_.setButtonText("limpiar");
  useEngineButton_.setButtonText("activar + start");
  freezeToggle_.setButtonText("freeze");
  pauseToggle_.setButtonText("pause");

  for (auto* b : {&loadButton_, &clearButton_, &useEngineButton_}) {
    b->addListener(this);
    b->setColour(juce::TextButton::buttonColourId, juce::Colour(0x14ffffff));
    b->setColour(juce::TextButton::textColourOffId, kText);
    addAndMakeVisible(b);
  }
  freezeToggle_.addListener(this);
  pauseToggle_.addListener(this);
  freezeToggle_.setColour(juce::ToggleButton::textColourId, kText);
  pauseToggle_.setColour(juce::ToggleButton::textColourId, kText);
  addAndMakeVisible(freezeToggle_);
  addAndMakeVisible(pauseToggle_);

  addSlider("px.main_volume", "main vol", -24.0, 12.0, 0.1);
  addSlider("px.stretch", "stretch", 0.1, 1024.0, 0.01);
  addSlider("px.fft_size", "fft size", 0.0, 1.0, 0.001);
  addSlider("px.pitch_shift", "pitch", -2400.0, 2400.0, 1.0);
  addSlider("px.freq_shift", "freq shift", -1000.0, 1000.0, 1.0);
  addSlider("px.spread", "spread", 0.0, 1.0, 0.001);
  addSlider("px.filter_low", "filter low", 0.0, 22000.0, 1.0);
  addSlider("px.filter_high", "filter high", 0.0, 22000.0, 1.0);
  addSlider("px.onset", "onset", 0.0, 1.0, 0.001);
  // sound_start / sound_end controlled via SourceWaveform at bottom

  addToggle("px.enab_harmonics", "harmonics");
  addToggle("px.enab_octaves", "octaves");
  addToggle("px.enab_freqshift", "freq shift");
  addToggle("px.enab_pitchshift", "pitch shift");
  addToggle("px.enab_spread", "spread");
  addToggle("px.enab_filter", "filter");
  addToggle("px.enab_freefilter", "free filter");
  addToggle("px.enab_ratiomix", "ratio mix");
  addToggle("px.enab_compressor", "compressor");

  sourceWaveform_ = std::make_unique<SourceWaveform>(processor_);
  addAndMakeVisible(*sourceWaveform_);

  refreshFromEngine();
  startTimerHz(12);
}

PaulXStretchPanel::~PaulXStretchPanel() {
  stopTimer();
  for (auto* b : {&loadButton_, &clearButton_, &useEngineButton_}) b->removeListener(this);
  freezeToggle_.removeListener(this);
  pauseToggle_.removeListener(this);
  for (auto& row : sliders_) row->slider.removeListener(this);
}

void PaulXStretchPanel::setBrandFont(const juce::Font& font) {
  brandFont_ = font;
  title_.setFont(brandFont_.withHeight(15.0f));
  subtitle_.setFont(brandFont_.withHeight(11.0f));
  styleLabel(sourceLabel_, brandFont_, 0.85f);
  styleLabel(playheadLabel_, brandFont_, 0.75f);
  styleLabel(modulesLabel_, brandFont_, 0.9f);
  for (auto& row : sliders_) styleLabel(row->label, brandFont_, 0.9f);
}

PaulXStretchPanel::SliderRow* PaulXStretchPanel::findSlider(const juce::String& id) {
  for (auto& row : sliders_)
    if (row->paramId == id) return row.get();
  return nullptr;
}

void PaulXStretchPanel::addSlider(const juce::String& id, const juce::String& label, double minV,
                                  double maxV, double step) {
  auto row = std::make_unique<SliderRow>();
  row->paramId = id;
  row->label.setText(label, juce::dontSendNotification);
  styleLabel(row->label, brandFont_, 0.9f);
  row->slider.setRange(minV, maxV, step);
  row->slider.setSliderStyle(juce::Slider::LinearHorizontal);
  row->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 16);
  row->slider.setColour(juce::Slider::thumbColourId, juce::Colours::white);
  row->slider.setColour(juce::Slider::trackColourId, kAccent.withAlpha(0.75f));
  row->slider.setColour(juce::Slider::backgroundColourId, juce::Colour(0x33ffffff));
  row->slider.setColour(juce::Slider::textBoxTextColourId, kMuted);
  row->slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
  row->slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
  row->slider.addListener(this);
  addAndMakeVisible(row->label);
  addAndMakeVisible(row->slider);
  sliders_.push_back(std::move(row));
}

void PaulXStretchPanel::addToggle(const juce::String& id, const juce::String& label) {
  auto row = std::make_unique<ToggleRow>();
  row->paramId = id;
  row->toggle.setButtonText(label);
  row->toggle.setColour(juce::ToggleButton::textColourId, kText);
  row->toggle.setColour(juce::ToggleButton::tickColourId, kAccent);
  row->toggle.addListener(this);
  addAndMakeVisible(row->toggle);
  toggles_.push_back(std::move(row));
}

void PaulXStretchPanel::setShowTitle(bool show) {
  title_.setVisible(show);
  resized();
}

void PaulXStretchPanel::paint(juce::Graphics& g) {
  g.setColour(kGlass);
  g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
  g.setColour(kGlassStroke);
  g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 8.0f, 1.0f);
}

void PaulXStretchPanel::resized() {
  auto area = getLocalBounds().reduced(12);
  auto header = area.removeFromTop(22);
  if (title_.isVisible())
    title_.setBounds(header.removeFromLeft(130));
  else
    title_.setBounds({});
  useEngineButton_.setBounds(header.removeFromRight(140));
  header.removeFromRight(6);
  clearButton_.setBounds(header.removeFromRight(70));
  header.removeFromRight(4);
  loadButton_.setBounds(header.removeFromRight(110));

  area.removeFromTop(2);
  subtitle_.setBounds(area.removeFromTop(16));
  sourceLabel_.setBounds(area.removeFromTop(16));
  playheadLabel_.setBounds(area.removeFromTop(16));

  area.removeFromTop(kGap);
  auto transport = area.removeFromTop(kRowH);
  freezeToggle_.setBounds(transport.removeFromLeft(80));
  transport.removeFromLeft(8);
  pauseToggle_.setBounds(transport.removeFromLeft(80));

  area.removeFromTop(kGap);
  if (sourceWaveform_ != nullptr) {
    sourceWaveform_->setBounds(area.removeFromBottom(kWaveH));
    area.removeFromBottom(kGap);
  }

  const int leftW = juce::jmax(280, area.getWidth() * 58 / 100);
  auto left = area.removeFromLeft(leftW);
  area.removeFromLeft(kGap);

  for (auto& row : sliders_) {
    auto r = left.removeFromTop(kRowH);
    row->label.setBounds(r.removeFromLeft(110));
    row->slider.setBounds(r);
    left.removeFromTop(2);
  }

  modulesLabel_.setBounds(area.removeFromTop(18));
  area.removeFromTop(2);
  for (auto& row : toggles_) {
    row->toggle.setBounds(area.removeFromTop(kRowH));
    area.removeFromTop(2);
  }
}

void PaulXStretchPanel::refreshFromEngine() {
  syncing_ = true;
  const auto name = processor_.paulXStretchSourceName();
  sourceLabel_.setText(name.isNotEmpty() ? (U8("Fuente: ") + name) : U8("Fuente: (ninguna)"),
                       juce::dontSendNotification);

  for (auto& row : sliders_) {
    float v = 0.0f;
    if (processor_.getPaulXStretchParameter(row->paramId, v))
      row->slider.setValue(v, juce::dontSendNotification);
  }

  float freeze = 0.0f, pause = 0.0f;
  processor_.getPaulXStretchParameter("px.freeze", freeze);
  processor_.getPaulXStretchParameter("px.pause", pause);
  freezeToggle_.setToggleState(freeze >= 0.5f, juce::dontSendNotification);
  pauseToggle_.setToggleState(pause >= 0.5f, juce::dontSendNotification);

  for (auto& row : toggles_) {
    float v = 0.0f;
    if (processor_.getPaulXStretchParameter(row->paramId, v))
      row->toggle.setToggleState(v >= 0.5f, juce::dontSendNotification);
  }

  if (sourceWaveform_ != nullptr) sourceWaveform_->reloadFromProcessor();

  setEnabled(processor_.hasPaulXStretch());
  syncing_ = false;
}

void PaulXStretchPanel::timerCallback() {
  playheadLabel_.setText(
      U8("Playhead: ") + juce::String(processor_.paulXStretchPlayheadPercent() * 100.0, 1) +
          "%   |   Prebuffer: " +
          juce::String(processor_.paulXStretchPrebufferReady() * 100.0, 0) + "%" +
          (processor_.isEngineOn() ? U8("   |   Engine ON") : U8("   |   Engine OFF")),
      juce::dontSendNotification);
  if (sourceWaveform_ != nullptr)
    sourceWaveform_->setPlayhead(static_cast<float>(processor_.paulXStretchPlayheadPercent()));
}

void PaulXStretchPanel::pushSlider(SliderRow& row) {
  if (syncing_) return;
  processor_.setPaulXStretchParameter(row.paramId, static_cast<float>(row.slider.getValue()));
}

void PaulXStretchPanel::pushToggle(ToggleRow& row) {
  if (syncing_) return;
  processor_.setPaulXStretchParameter(row.paramId, row.toggle.getToggleState() ? 1.0f : 0.0f);
}

void PaulXStretchPanel::sliderValueChanged(juce::Slider* slider) {
  for (auto& row : sliders_) {
    if (&row->slider == slider) {
      pushSlider(*row);
      return;
    }
  }
}

void PaulXStretchPanel::chooseFile() {
  fileChooser_ = std::make_unique<juce::FileChooser>(
      U8("Audio para PaulXStretch"), juce::File{}, "*.wav;*.aif;*.aiff;*.flac;*.ogg");
  constexpr auto flags =
      juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
  fileChooser_->launchAsync(flags, [this](const juce::FileChooser& fc) {
    const auto f = fc.getResult();
    if (!f.existsAsFile()) return;
    juce::String status;
    processor_.loadPaulXStretchSource(f, status);
    refreshFromEngine();
  });
}

void PaulXStretchPanel::buttonClicked(juce::Button* button) {
  if (button == &loadButton_) {
    chooseFile();
    return;
  }
  if (button == &clearButton_) {
    processor_.clearPaulXStretchSource();
    refreshFromEngine();
    return;
  }
  if (button == &useEngineButton_) {
    processor_.setPaulXStretchEnabled(true);
    processor_.setPaulXStretchParameter("px.pause", 0.0f);
    if (!processor_.isEngineOn()) processor_.setEngineOn(true);
    return;
  }
  if (button == &freezeToggle_) {
    processor_.setPaulXStretchParameter("px.freeze", freezeToggle_.getToggleState() ? 1.0f : 0.0f);
    return;
  }
  if (button == &pauseToggle_) {
    processor_.setPaulXStretchParameter("px.pause", pauseToggle_.getToggleState() ? 1.0f : 0.0f);
    return;
  }
  for (auto& row : toggles_) {
    if (&row->toggle == button) {
      pushToggle(*row);
      return;
    }
  }
}
