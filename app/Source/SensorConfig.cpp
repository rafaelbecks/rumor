#include "SensorConfig.h"
#include "PluginProcessor.h"

#include <cmath>

namespace {
const juce::Colour kText{0xfff4f1ea};
const juce::Colour kMuted{0xffa7a39b};
const juce::Colour kStroke{0x22ffffff};
const juce::Colour kAccent{0xffc4a0ff};

juce::String U8(const char* utf8) { return juce::String::fromUTF8(utf8); }

void styleLabel(juce::Label& label, const juce::Font& font, juce::Colour colour) {
  label.setFont(font);
  label.setColour(juce::Label::textColourId, colour);
  label.setJustificationType(juce::Justification::centredLeft);
}
}  // namespace

SensorConfigComponent::KeyRow::KeyRow() {
  enable.setClickingTogglesState(true);
  addAndMakeVisible(enable);
  name.setJustificationType(juce::Justification::centredLeft);
  name.setColour(juce::Label::textColourId, kText);
  addAndMakeVisible(name);
  reading.setJustificationType(juce::Justification::centredRight);
  reading.setColour(juce::Label::textColourId, kAccent);
  reading.setText(juce::String::fromUTF8("\xe2\x80\x94"), juce::dontSendNotification);
  addAndMakeVisible(reading);
  for (auto* editor : {&low, &high}) {
    editor->setColour(juce::TextEditor::backgroundColourId, juce::Colour(0x14ffffff));
    editor->setColour(juce::TextEditor::textColourId, kText);
    editor->setColour(juce::TextEditor::outlineColourId, kStroke);
    editor->setJustification(juce::Justification::centredLeft);
    addAndMakeVisible(editor);
  }
  low.setText("0", false);
  high.setText("1", false);
}

void SensorConfigComponent::KeyRow::resized() {
  auto r = getLocalBounds();
  enable.setBounds(r.removeFromLeft(28));
  high.setBounds(r.removeFromRight(72));
  r.removeFromRight(4);
  low.setBounds(r.removeFromRight(72));
  r.removeFromRight(6);
  reading.setBounds(r.removeFromRight(72));
  r.removeFromRight(4);
  name.setBounds(r);
}

SensorConfigComponent::SensorConfigComponent(UmbralAudioProcessor& processor, juce::Font font,
                                             std::function<void()> onSaved)
    : processor_(processor),
      font_(std::move(font)),
      onSaved_(std::move(onSaved)),
      serialTitle_({}, "serial"),
      portLabel_({}, "puerto"),
      refreshButton_("actualizar"),
      baudLabel_({}, "baud"),
      inputLabel_({}, "entrada"),
      wsTitle_({}, "websocket"),
      hostLabel_({}, "host"),
      keyHint_({}, juce::String::fromUTF8("Las claves aparecen al recibir JSON.")),
      remoteTitle_({}, "remoto"),
      remoteLabel_({}, "host"),
      remoteButton_("vincular"),
      saveButton_("guardar"),
      closeButton_("cerrar") {
  for (auto* label : {&serialTitle_, &wsTitle_, &remoteTitle_})
    styleLabel(*label, font_.withHeight(14.0f), kAccent);
  for (auto* label : {&portLabel_, &baudLabel_, &inputLabel_, &hostLabel_, &remoteLabel_, &keyHint_})
    styleLabel(*label, font_, kMuted);
  keyHint_.setMinimumHorizontalScale(0.7f);
  errorLabel_.setFont(font_.withHeight(12.0f));
  errorLabel_.setColour(juce::Label::textColourId, juce::Colours::orange);
  errorLabel_.setJustificationType(juce::Justification::centredLeft);

  for (auto* button : {&refreshButton_, &remoteButton_, &saveButton_, &closeButton_}) {
    button->addListener(this);
    button->setColour(juce::TextButton::buttonColourId, juce::Colour(0x14ffffff));
    button->setColour(juce::TextButton::textColourOffId, kText);
    addAndMakeVisible(button);
  }
  addAndMakeVisible(serialTitle_);
  addAndMakeVisible(portLabel_);
  addAndMakeVisible(portBox_);
  addAndMakeVisible(baudLabel_);
  addAndMakeVisible(baudBox_);
  addAndMakeVisible(inputLabel_);
  addAndMakeVisible(wsTitle_);
  addAndMakeVisible(hostLabel_);
  addAndMakeVisible(remoteTitle_);
  addAndMakeVisible(remoteLabel_);
  addAndMakeVisible(errorLabel_);

  baudBox_.addItemList({"9600", "19200", "38400", "57600", "115200"}, 1);
  const int baudIds[] = {9600, 19200, 38400, 57600, 115200};
  const auto cfg = processor_.getSerialConfig();
  baudBox_.setSelectedId(1, juce::dontSendNotification);
  for (int i = 0; i < 5; ++i)
    if (baudIds[i] == cfg.baud) baudBox_.setSelectedItemIndex(i, juce::dontSendNotification);

  styleEditor(inputLow_, "lo");
  styleEditor(inputHigh_, "hi");
  inputLow_.setText(juce::String(cfg.inputLow), false);
  inputHigh_.setText(juce::String(cfg.inputHigh), false);
  addAndMakeVisible(inputLow_);
  addAndMakeVisible(inputHigh_);

  styleEditor(hostEdit_, "ws://host:port/path");
  hostEdit_.setText(juce::String(cfg.wsUrl), false);
  addAndMakeVisible(hostEdit_);
  addAndMakeVisible(keyHint_);

  styleEditor(remoteEdit_, "https://rumor-remote.onrender.com");
  remoteEdit_.setText(processor_.getRemoteUrl(), false);
  addAndMakeVisible(remoteEdit_);
  remoteButton_.setButtonText(processor_.isRemoteEnabled() ? "cortar" : "vincular");

  keyViewport_.setViewedComponent(&keyList_, false);
  keyViewport_.setScrollBarsShown(true, false);
  addAndMakeVisible(keyViewport_);

  refreshPorts();
  rebuildKeys();
  startTimerHz(4);
}

SensorConfigComponent::~SensorConfigComponent() {
  stopTimer();
  for (auto* button : {&refreshButton_, &remoteButton_, &saveButton_, &closeButton_})
    button->removeListener(this);
}

void SensorConfigComponent::styleEditor(juce::TextEditor& editor, const juce::String& empty) {
  editor.setFont(font_);
  editor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0x14ffffff));
  editor.setColour(juce::TextEditor::textColourId, kText);
  editor.setColour(juce::TextEditor::outlineColourId, kStroke);
  editor.setColour(juce::TextEditor::focusedOutlineColourId, kAccent.withAlpha(0.55f));
  editor.setTextToShowWhenEmpty(empty, kMuted);
  editor.setJustification(juce::Justification::centredLeft);
}

void SensorConfigComponent::paint(juce::Graphics& g) {
  g.fillAll(juce::Colour(0xff141416));
  g.setColour(kStroke);
  auto line = [this, &g](int y) { g.drawHorizontalLine(y, 16.0f, static_cast<float>(getWidth() - 16)); };
  line(serialTitle_.getBottom() + 2);
  line(wsTitle_.getBottom() + 2);
  line(remoteTitle_.getBottom() + 2);
}

void SensorConfigComponent::refreshPorts() {
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
      if (portPaths_[i] == selected && selected.isNotEmpty()) break;
    }
  if (cfgPort.isNotEmpty() && match == 0) {
    portBox_.addItem(cfgPort, id);
    portPaths_.add(cfgPort);
    match = portPaths_.size() - 1;
  }
  portBox_.setSelectedItemIndex(match, juce::dontSendNotification);
}

void SensorConfigComponent::rebuildKeys() {
  std::vector<glow::WsInput> rows;
  juce::String signature;
  for (const auto& row : keyRows_) {
    glow::WsInput input;
    input.key = row->name.getText().toStdString();
    input.enabled = row->enable.getToggleState();
    input.low = row->low.getText().trim().getFloatValue();
    input.high = row->high.getText().trim().getFloatValue();
    rows.push_back(input);
    signature << input.key << "\n";
  }

  auto remember = [&rows](const glow::WsInput& input) {
    for (const auto& existing : rows)
      if (existing.key == input.key) return;
    rows.push_back(input);
  };
  for (const auto& saved : processor_.getSerialConfig().wsKeys) remember(saved);
  for (const auto& seen : processor_.getWsSeenKeys()) {
    glow::WsInput input;
    input.key = seen;
    input.low = 0.0f;
    input.high = 1.0f;
    input.enabled = false;
    remember(input);
  }

  juce::String nextSignature;
  for (const auto& input : rows) nextSignature << input.key << "\n";
  if (nextSignature == keySignature_ && !keyRows_.empty()) return;
  if (nextSignature == keySignature_ && rows.empty() && keyRows_.empty()) return;
  keySignature_ = nextSignature;

  keyRows_.clear();
  keyList_.removeAllChildren();
  for (const auto& input : rows) {
    auto row = std::make_unique<KeyRow>();
    row->name.setText(juce::String(input.key), juce::dontSendNotification);
    row->name.setFont(font_);
    row->reading.setFont(font_);
    row->enable.setToggleState(input.enabled, juce::dontSendNotification);
    row->low.setFont(font_);
    row->high.setFont(font_);
    row->low.setText(juce::String(input.low, 4), false);
    row->high.setText(juce::String(input.high, 4), false);
    keyList_.addAndMakeVisible(*row);
    keyRows_.push_back(std::move(row));
  }
  resized();
}

bool SensorConfigComponent::collect(juce::String& error) {
  auto cfg = processor_.getSerialConfig();
  const int portIndex = portBox_.getSelectedItemIndex();
  cfg.port.clear();
  if (portIndex > 0 && portIndex < portPaths_.size()) cfg.port = portPaths_[portIndex].toStdString();
  const int baudIds[] = {9600, 19200, 38400, 57600, 115200};
  cfg.baud = baudIds[juce::jlimit(0, 4, baudBox_.getSelectedItemIndex())];

  const auto lowText = inputLow_.getText().trim();
  const auto highText = inputHigh_.getText().trim();
  if (lowText.isEmpty() || highText.isEmpty() || !lowText.containsOnly("0123456789") ||
      !highText.containsOnly("0123456789")) {
    error = U8("La entrada serial necesita dos enteros distintos, cada uno 0-1000000.");
    return false;
  }
  cfg.inputLow = lowText.getIntValue();
  cfg.inputHigh = highText.getIntValue();
  if (cfg.inputLow == cfg.inputHigh || cfg.inputLow < 0 || cfg.inputHigh < 0 || cfg.inputLow > 1000000 ||
      cfg.inputHigh > 1000000) {
    error = U8("La entrada serial necesita dos enteros distintos, cada uno 0-1000000.");
    return false;
  }

  cfg.wsUrl = hostEdit_.getText().trim().toStdString();
  cfg.wsKeys.clear();
  for (const auto& row : keyRows_) {
    glow::WsInput input;
    input.key = row->name.getText().trim().toStdString();
    if (input.key.empty()) continue;
    input.enabled = row->enable.getToggleState();
    input.low = row->low.getText().trim().getFloatValue();
    input.high = row->high.getText().trim().getFloatValue();
    const bool emptySpan = !(input.low < input.high) && !(input.high < input.low);
    if (emptySpan) {
      error = U8("Rango invalido para ") + juce::String(input.key) + ".";
      return false;
    }
    cfg.wsKeys.push_back(std::move(input));
  }

  juce::String status;
  if (!processor_.saveConnectionSettings(cfg, status)) {
    error = status;
    return false;
  }
  processor_.setRemoteUrl(remoteEdit_.getText().trim());
  error = status;
  return true;
}

void SensorConfigComponent::closeWith(int code) {
  if (auto* window = findParentComponentOfClass<juce::DialogWindow>()) window->exitModalState(code);
}

void SensorConfigComponent::buttonClicked(juce::Button* button) {
  if (button == &refreshButton_) {
    refreshPorts();
    return;
  }
  if (button == &remoteButton_) {
    if (processor_.isRemoteEnabled()) {
      processor_.disconnectRemote();
    } else {
      const auto url = remoteEdit_.getText().trim();
      if (url.isEmpty()) {
        errorLabel_.setText(U8("URL de remoto vacia."), juce::dontSendNotification);
        return;
      }
      processor_.connectRemote(url);
    }
    remoteButton_.setButtonText(processor_.isRemoteEnabled() ? "cortar" : "vincular");
    errorLabel_.setText({}, juce::dontSendNotification);
    return;
  }
  if (button == &closeButton_) {
    closeWith(0);
    return;
  }
  if (button == &saveButton_) {
    juce::String error;
    if (!collect(error)) {
      errorLabel_.setText(error, juce::dontSendNotification);
      return;
    }
    if (onSaved_) onSaved_();
    closeWith(1);
  }
}

void SensorConfigComponent::timerCallback() {
  remoteButton_.setButtonText(processor_.isRemoteEnabled() ? "cortar" : "vincular");
  remoteButton_.setColour(juce::TextButton::buttonColourId, processor_.isRemoteConnected()
                                                                ? kAccent.withAlpha(0.28f)
                                                                : juce::Colour(0x14ffffff));
  const auto before = keySignature_;
  rebuildKeys();
  if (keySignature_ != before) resized();

  const auto latest = processor_.getWsLatest();
  for (auto& row : keyRows_) {
    const auto key = row->name.getText().toStdString();
    juce::String text = juce::String::fromUTF8("\xe2\x80\x94");
    for (const auto& sample : latest) {
      if (sample.first != key) continue;
      const float value = sample.second;
      text = std::abs(value - std::round(value)) < 0.05f ? juce::String(juce::roundToInt(value))
                                                         : juce::String(value, 2);
      break;
    }
    if (row->reading.getText() != text) row->reading.setText(text, juce::dontSendNotification);
  }
}

void SensorConfigComponent::resized() {
  auto area = getLocalBounds().reduced(16, 14);
  serialTitle_.setBounds(area.removeFromTop(22));
  area.removeFromTop(8);
  auto port = area.removeFromTop(26);
  portLabel_.setBounds(port.removeFromLeft(52));
  refreshButton_.setBounds(port.removeFromRight(92));
  port.removeFromRight(6);
  portBox_.setBounds(port);
  area.removeFromTop(6);
  auto baud = area.removeFromTop(26);
  baudLabel_.setBounds(baud.removeFromLeft(52));
  baudBox_.setBounds(baud.removeFromLeft(110));
  baud.removeFromLeft(12);
  inputLabel_.setBounds(baud.removeFromLeft(58));
  inputLow_.setBounds(baud.removeFromLeft(72));
  baud.removeFromLeft(6);
  inputHigh_.setBounds(baud.removeFromLeft(72));
  area.removeFromTop(16);

  wsTitle_.setBounds(area.removeFromTop(22));
  area.removeFromTop(8);
  auto host = area.removeFromTop(26);
  hostLabel_.setBounds(host.removeFromLeft(52));
  hostEdit_.setBounds(host);
  area.removeFromTop(6);
  const int keysH = juce::jlimit(88, 220, 28 + static_cast<int>(keyRows_.size()) * 28);
  keyHint_.setVisible(keyRows_.empty());
  keyHint_.setBounds(keyRows_.empty() ? area.removeFromTop(32) : juce::Rectangle<int>());
  keyViewport_.setVisible(!keyRows_.empty());
  keyViewport_.setBounds(keyRows_.empty() ? juce::Rectangle<int>() : area.removeFromTop(keysH));
  int y = 0;
  const int w = juce::jmax(120, keyViewport_.getWidth() - 8);
  for (auto& row : keyRows_) {
    row->setBounds(0, y, w, 26);
    y += 28;
  }
  keyList_.setSize(w, juce::jmax(y, keyViewport_.getHeight()));
  area.removeFromTop(16);

  remoteTitle_.setBounds(area.removeFromTop(22));
  area.removeFromTop(8);
  auto remote = area.removeFromTop(26);
  remoteLabel_.setBounds(remote.removeFromLeft(52));
  remoteButton_.setBounds(remote.removeFromRight(80));
  remote.removeFromRight(6);
  remoteEdit_.setBounds(remote);

  auto buttons = area.removeFromBottom(28);
  errorLabel_.setBounds(area.reduced(0, 6));
  closeButton_.setBounds(buttons.removeFromRight(80));
  buttons.removeFromRight(6);
  saveButton_.setBounds(buttons.removeFromRight(88));
}
