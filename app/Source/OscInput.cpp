#include "OscInput.h"

namespace {

bool readNormalized(const juce::OSCMessage& message, float& value) {
  if (message.size() < 1) return false;
  const auto& arg = message[0];
  if (arg.isFloat32()) {
    value = arg.getFloat32();
    return true;
  }
  if (arg.isInt32()) {
    value = static_cast<float>(arg.getInt32());
    return true;
  }
  return false;
}

juce::String bindLabel(const juce::String& host, int port) {
  const auto shown = host.isEmpty() ? juce::String("0.0.0.0") : host;
  return "osc: " + shown + ":" + juce::String(port);
}

}  // namespace

OscInput::OscInput() { receiver_.addListener(this); }

OscInput::~OscInput() {
  receiver_.removeListener(this);
  receiver_.disconnect();
}

void OscInput::setHandler(Handler handler) { handler_ = std::move(handler); }

bool OscInput::listen(const juce::String& host, int port, juce::String& error) {
  stop();
  if (port < 1 || port > 65535) {
    error = "El puerto OSC tiene que estar entre 1 y 65535.";
    status_ = error;
    return false;
  }

  const auto trimmed = host.trim();
  const bool any = trimmed.isEmpty() || trimmed == "0.0.0.0" || trimmed == "*" || trimmed == "any";
  bool ok = false;
  if (any) {
    ok = receiver_.connect(port);
  } else {
    socket_ = std::make_unique<juce::DatagramSocket>(false);
    ok = socket_->bindToPort(port, trimmed) && receiver_.connectToSocket(*socket_);
    if (!ok) socket_.reset();
  }

  if (!ok) {
    error = "OSC: no se pudo abrir el puerto " + juce::String(port) + ".";
    status_ = error;
    listening_ = false;
    return false;
  }

  listening_ = true;
  status_ = bindLabel(any ? "0.0.0.0" : trimmed, port);
  error = status_;
  return true;
}

void OscInput::stop() {
  receiver_.disconnect();
  socket_.reset();
  listening_ = false;
  if (status_.startsWith("osc:")) status_ = "osc: en pausa";
}

void OscInput::oscMessageReceived(const juce::OSCMessage& message) { handleMessage(message); }

void OscInput::oscBundleReceived(const juce::OSCBundle& bundle) {
  for (int i = 0; i < bundle.size(); ++i) {
    const auto& element = bundle[i];
    if (element.isMessage()) handleMessage(element.getMessage());
    else if (element.isBundle()) oscBundleReceived(element.getBundle());
  }
}

void OscInput::handleMessage(const juce::OSCMessage& message) {
  float value = 0.0f;
  if (!readNormalized(message, value) || handler_ == nullptr) return;
  handler_(message.getAddressPattern().toString(), value);
}
