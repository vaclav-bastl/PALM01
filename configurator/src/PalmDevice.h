// exoPALM configurator -- device connection over direct MIDI access.
// Opens the MIDI port itself (JUCE wraps CoreMIDI on macOS), bypassing DAW
// routing: scans for "exohub <name>" first (ESP-NOW path), then
// "<name> Bluetooth" (direct BLE). Async request/reply with reassembled
// SysEx frames arriving via juce::MidiInputCallback.
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "PalmProtocol.h"

class PalmDevice : private juce::MidiInputCallback,
                   private juce::Timer {
public:
    struct Listener {
        virtual ~Listener() = default;
        virtual void deviceInfoReceived(const palm::DeviceInfo&) = 0;
        virtual void configReceived(const palm::Config&) = 0;
        virtual void connectionChanged() = 0;
        virtual void midiActivity(const juce::MidiMessage&) {}  // monitor pane
    };

    PalmDevice() { startTimer(1500); }
    ~PalmDevice() override { close(); }

    void setListener(Listener* l) { listener = l; }

    // Available candidate ports (paired in/out by matching names)
    struct Port { juce::String name; juce::String inId, outId; bool isBluetooth = false; };
    juce::Array<Port> scanPorts() const;

    bool open(const Port& port);
    void close();
    bool isOpen() const { return midiOut != nullptr && midiIn != nullptr; }
    juce::String openPortName() const { return currentPort.name; }
    bool openPortIsBluetooth() const { return currentPort.isBluetooth; }

    void requestInfo()               { send(palm::buildInfoRequest()); }
    void requestDump()               { send(palm::buildDumpRequest()); }
    void setParam(int addr, uint8_t v) { send(palm::buildSetParam(addr, v)); }
    void saveToDevice()              { send(palm::buildSave()); }
    void setName(const juce::String& n) { send(palm::buildSetName(n.toStdString())); }
    void revert()                    { send(palm::buildRevert()); }
    void setEditMode(bool on)        { send(palm::buildEditMode(on)); }

private:
    void send(const std::vector<uint8_t>& bytes);
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;
    void timerCallback() override;  // detects port disappearance

    std::unique_ptr<juce::MidiInput> midiIn;
    std::unique_ptr<juce::MidiOutput> midiOut;
    Port currentPort;
    Listener* listener = nullptr;
};
