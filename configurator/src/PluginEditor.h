// exoPALM configurator -- UI per vst-design/UImockup.png: Eightgon type,
// white on black, one screen whose layout follows the preset mode (CC vs
// NOTE), disabled parameters in grey, live MIDI monitor at the bottom.
// The whole UI lives on a fixed 524x566 canvas that scales with the window.
#pragma once
#include "PluginProcessor.h"
#include "BinaryData.h"
#include <deque>
#include <map>
#include <memory>
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

// A labeled value row: "wrist cc: 15". Drag vertically to change,
// double-click to type. Disableable rows reach "off" (255) by dragging
// below the minimum and render grey; allow255Above rows reach 255 by
// dragging above the maximum ("hang"/"gate" via formatter, not grey).
// hasIcon rows lead with a MIDI DIN plug; clicking it fires onIconClick
// (the editor sends the mapped MIDI message to the host for MIDI-learn).
class ParamRow : public juce::Component {
public:
    ParamRow() { setWantsKeyboardFocus(true); }  // so open text boxes lose focus

    juce::String label;
    juce::Font font { juce::FontOptions{} };
    bool disableable = false;     // 255 = off, via drag below min
    bool allow255Above = false;   // 255 = hang/gate, via drag above max
    bool hasIcon = false;         // MIDI DIN plug, click = onIconClick + text box
    bool toggleOnClick = false;   // two-state row: single click flips it
    bool readOnly = false;        // display only (chord name row)
    uint8_t minV = 0, maxV = 127;
    int valueX = 106;             // mockup value column, relative to row origin
    std::function<void()> onIconClick;
    std::function<void(uint8_t)> onValue;
    std::function<juce::String(uint8_t)> formatter;  // optional display override

    void setRaw(uint8_t r) { raw = r; repaint(); }
    uint8_t getRaw() const { return raw; }
    bool isOff() const { return disableable && raw == 255; }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    uint8_t raw = 0;
    int dragStartVal = 0;
    std::unique_ptr<juce::TextEditor> edit;
    bool committing = false;
    void openEdit();
    void commitEdit(bool applyText);
    void apply(uint8_t v) { raw = v; repaint(); if (onValue) onValue(v); }
};

// A label with a click action ("normal operation" flips edit-only back off)
struct ClickLabel : juce::Label {
    std::function<void()> onClick;
    void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); }
};

class ExoPalmEditor : public juce::AudioProcessorEditor,
                      private PalmDevice::Listener,
                      private juce::Timer {
public:
    explicit ExoPalmEditor(ExoPalmProcessor&);
    ~ExoPalmEditor() override;

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff0d0b0d)); }
    void resized() override;  // scales the fixed canvas

    static constexpr int kCanvasW = 524, kCanvasH = 566;

private:
    // All children live here at mockup coordinates; the editor only scales it.
    struct Canvas : juce::Component {
        Canvas() { setWantsKeyboardFocus(true); }  // clicks close open text boxes
        std::function<void(juce::Graphics&)> onPaint;
        std::function<void(const juce::MouseEvent&)> onMouse;
        void paint(juce::Graphics& g) override { if (onPaint) onPaint(g); }
        void mouseDown(const juce::MouseEvent& e) override {
            grabKeyboardFocus();
            if (onMouse) onMouse(e);
        }
    };

    void deviceInfoReceived(const palm::DeviceInfo&) override;
    void configReceived(const palm::Config&) override;
    void connectionChanged() override;
    void midiActivity(const juce::MidiMessage&) override;
    void timerCallback() override;

    void layoutCanvas();               // one-time, fixed coordinates
    void rescanPorts();                // + auto-connect to anything "palm"
    void openPortAt(int idx);
    void refreshFromConfig();          // config -> all rows
    void sendParam(int addr, uint8_t v);
    void sendIconCc(const ParamRow& r);   // DIN icon -> host MIDI out
    void canvasPaint(juce::Graphics&);
    void canvasMouse(const juce::MouseEvent&);

    bool noteMode() const { return cfg().get(palm::addrPreset(preset, 0)) != 0; }
    int midiChannel() const;
    palm::Config& cfg() { return proc.lastConfig; }
    const palm::Config& cfg() const { return proc.lastConfig; }

    // note-mode roots: 7 fingers = white keys c..b (mirrors with the hand)
    int rootFinger(int whiteIdx) const;
    int rootIndexForFinger(int f) const;   // -1 if not a root pad
    uint16_t chordMask() const;            // selected pad's chord (or default triad)
    void toggleChordBit(int pc);

    void drawDevice(juce::Graphics&);
    void drawHand(juce::Graphics&);
    void drawConnIcons(juce::Graphics&);
    void drawChordGrid(juce::Graphics&);

    ExoPalmProcessor& proc;
    PalmLookAndFeel lnf;
    juce::Font eightgon, eightgonBig;
    Canvas canvas;

    // header
    juce::ComboBox portBox;
    ClickLabel opLabel;                 // click = back to normal operation
    juce::Label nameLabel, connLabel;
    juce::ToggleButton editOnly { "edit only output" };
    juce::TextButton saveBtn { "save" }, revertBtn { "revert" };

    // main column
    ParamRow presetRow, channelRow, modeRow;
    ParamRow gWristCc, gWristRel, gElbowCc, gElbowRel;
    juce::ComboBox fingerBox;
    juce::Label touchHeader;
    ParamRow tCc, tRange, fWristCc, fWristRel, fElbowCc, fElbowRel;
    ParamRow selectorRow, chordRow;     // note mode
    juce::Label handLabel;              // hand area click toggles left/right

    // monitor
    struct MonRow { juce::String msg, ch, num, val; };
    std::deque<MonRow> monitor;

    juce::Rectangle<int> padRects[8];      // finger pads, click = pick finger
    juce::Rectangle<int> presetRects[3];   // thumb points, click = pick preset
    juce::Rectangle<int> handArea;

    juce::Array<PalmDevice::Port> ports;
    int preset = 0, finger = 6;            // default INDEX_A like the mockup
    bool loadingUi = false;

    // connection probe: a "palm" port can exist while the device is linked
    // elsewhere (hub vs BLE) -- if a port stays silent, try the next one
    bool probing = false;
    juce::uint32 openedAt = 0;
    int autoIdx = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExoPalmEditor)
};
