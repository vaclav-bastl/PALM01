// exoPALM configurator -- UI per vst-design/UImockup.png: Eightgon type,
// white on black, one screen whose layout follows the preset mode (CC vs
// NOTE), disabled parameters in grey, live MIDI monitor at the bottom.
#pragma once
#include "PluginProcessor.h"
#include "BinaryData.h"
#include <deque>
#include <map>
#include <functional>

// Flat mockup styling: borderless combo boxes, text-only toggles and
// buttons, Eightgon in the popups too.
class PalmLookAndFeel : public juce::LookAndFeel_V4 {
public:
    juce::Font font { juce::FontOptions{} };
    PalmLookAndFeel();
    void drawComboBox(juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override {}
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override { return font; }
    juce::Font getPopupMenuFont() override { return font; }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return font; }
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override {}
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool, bool) override;
};

// A labeled value row: "wrist cc: 15". Drag vertically to change; if
// disableable, a MIDI DIN plug icon leads the row and clicking it toggles
// off (raw 255) -- the row then renders grey with value "off".
class ParamRow : public juce::Component {
public:
    juce::String label;
    juce::Font font { juce::FontOptions{} };
    bool disableable = false;   // 255 = off
    uint8_t minV = 0, maxV = 127;
    int valueX = 106;           // mockup value column, relative to row origin
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
    void mouseDown(const juce::MouseEvent&) override;  // hand toggle + pad select

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

    void drawDevice(juce::Graphics&);   // full-height sidebar silhouette
    void drawHand(juce::Graphics&);     // hand + strap, mirrored per side
    void drawConnIcons(juce::Graphics&);
    void drawChordGrid(juce::Graphics&);

    ExoPalmProcessor& proc;
    PalmLookAndFeel lnf;                // before components: they reference it
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
    ParamRow selectorRow, chordRow;     // note mode
    juce::Label handLabel;              // click toggles left/right

    // monitor
    struct MonRow { juce::String msg, ch, num, val; };
    std::deque<MonRow> monitor;

    juce::Rectangle<int> padRects[8];   // sidebar octagons, click = pick finger
    juce::Rectangle<int> handArea;

    juce::Array<PalmDevice::Port> ports;
    int preset = 0, finger = 6;         // default INDEX_A like the mockup
    std::map<int, uint8_t> dirty;       // held back while "edit only output"
    bool loadingUi = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExoPalmEditor)
};
