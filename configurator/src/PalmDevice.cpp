#include "PalmDevice.h"

juce::Array<PalmDevice::Port> PalmDevice::scanPorts() const {
    juce::Array<Port> out;
    auto ins = juce::MidiInput::getAvailableDevices();
    auto outs = juce::MidiOutput::getAvailableDevices();
    // a usable port = same display name present as input AND output
    for (auto& i : ins) {
        for (auto& o : outs) {
            if (i.name != o.name) continue;
            bool hub = i.name.startsWith("exohub");
            bool bt = i.name.containsIgnoreCase("Bluetooth");
            if (!hub && !bt) continue;  // only PALM-reachable transports
            Port p;
            p.name = i.name;
            p.inId = i.identifier;
            p.outId = o.identifier;
            p.isBluetooth = bt;
            out.add(p);
        }
    }
    // prefer exohub ports first (lower latency, always-on)
    std::stable_sort(out.begin(), out.end(),
                     [](const Port& a, const Port& b) { return !a.isBluetooth && b.isBluetooth; });
    return out;
}

bool PalmDevice::open(const Port& port) {
    close();
    midiIn = juce::MidiInput::openDevice(port.inId, this);
    midiOut = juce::MidiOutput::openDevice(port.outId);
    if (midiIn == nullptr || midiOut == nullptr) { close(); return false; }
    currentPort = port;
    midiIn->start();
    if (listener) listener->connectionChanged();
    return true;
}

void PalmDevice::close() {
    if (midiIn) midiIn->stop();
    midiIn.reset();
    midiOut.reset();
    currentPort = {};
    if (listener) listener->connectionChanged();
}

void PalmDevice::send(const std::vector<uint8_t>& bytes) {
    if (midiOut)
        midiOut->sendMessageNow(juce::MidiMessage(bytes.data(), (int)bytes.size()));
}

void PalmDevice::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) {
    // JUCE reassembles SysEx into complete messages
    if (m.isSysEx()) {
        const uint8_t* d = m.getRawData();
        int len = m.getRawDataSize();
        if (palm::isPalmFrame(d, len)) {
            if (auto info = palm::parseInfo(d, len)) {
                juce::MessageManager::callAsync([this, i = *info] {
                    if (listener) listener->deviceInfoReceived(i);
                });
                return;
            }
            if (auto cfg = palm::parseDump(d, len)) {
                juce::MessageManager::callAsync([this, c = *cfg] {
                    if (listener) listener->configReceived(c);
                });
                return;
            }
        }
    }
    if (listener) {
        juce::MessageManager::callAsync([this, m] {
            if (listener) listener->midiActivity(m);
        });
    }
}

void PalmDevice::timerCallback() {
    if (!isOpen()) return;
    // if the opened port vanished (device unplugged / link change), close
    bool stillThere = false;
    for (auto& d : juce::MidiInput::getAvailableDevices())
        if (d.identifier == currentPort.inId) stillThere = true;
    if (!stillThere) close();
}
