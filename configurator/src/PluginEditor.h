// exoPALM configurator -- UI per vst-design/UImockup.png: Eightgon type,
// white on black, one screen whose layout follows the preset mode (CC vs
// NOTE), disabled parameters in grey, live MIDI monitor at the bottom.
#pragma once
#include "PluginProcessor.h"
#include "BinaryData.h"
#include <deque>
#include <map>
#include <functional>

// A labeled value row: "wrist cc: 15". Drag vertically (or click+type via
// double-click) to change; if disableable, clicking the leading dot toggles
// off (raw 255) and the row renders grey.
class ParamRow : public juce::Component {
public:
    juce::String label;
    juce::Font font { juce::FontOptions{} };
    bool disableable = false;   // 255 = off
    uint8_t minV = 0, maxV = 127;
    std::function<void(uint8_t)> onValue;
    std::function<juce::String(uint8_t)> formatter;  // optional display override

    void setRaw(uint8_t r) { raw = r; repaint(); }
    uint8_t getRaw() const { return raw; }
    bool isOff() const { return disableable && raw == 255; }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

private:
    uint8_t raw = 0, lastOn = 64;
    int dragStartVal = 0;
    void apply(uint8_t v) { raw = v; repaint(); if (onValue) onValue(v); }
};

class ExoPalmEditor : public juce::AudioProcessorEditor,
                      private PalmDevice::Listener,
                      private juce::Timer {
public:
    explicit ExoPalmEditor(ExoPalmProcessor&);
    ~ExoPalmEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;  // hand toggle

private:
    void deviceInfoReceived(const palm::DeviceInfo&) override;
    void configReceived(const palm::Config&) override;
    void connectionChanged() override;
    void midiActivity(const juce::MidiMessage&) override;
    void timerCallback() override;

    void rescanPorts();
    void refreshFromConfig();          // config -> all rows
    void sendParam(int addr, uint8_t v);
    bool noteMode() const { return cfg().get(palm::addrPreset(preset, 0)) != 0; }
    palm::Config& cfg() { return proc.lastConfig; }
    const palm::Config& cfg() const { return proc.lastConfig; }

    ExoPalmProcessor& proc;
    juce::Font eightgon, eightgonBig;

    // header
    juce::ComboBox portBox;
    juce::Label opLabel, nameLabel, connLabel;
    juce::ToggleButton editOnly { "edit only output" };
    juce::TextButton saveBtn { "save" }, revertBtn { "revert" };

    // main column
    ParamRow presetRow, channelRow, modeRow;
    ParamRow gWristCc, gWristRel, gElbowCc, gElbowRel;
    juce::ComboBox fingerBox;
    juce::Label touchHeader;
    ParamRow tCc, tRange, fWristCc, fWristRel, fElbowCc, fElbowRel;
    ParamRow selectorRow;               // note mode
    juce::Label chordLabel;             // note mode (fixed mapping)
    juce::Label handLabel;              // click toggles left/right

    // monitor
    struct MonRow { juce::String msg, ch, num, val; };
    std::deque<MonRow> monitor;

    juce::Array<PalmDevice::Port> ports;
    int preset = 0, finger = 6;         // default INDEX_A like the mockup
    std::map<int, uint8_t> dirty;       // held back while "edit only output"
    bool loadingUi = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExoPalmEditor)
};
