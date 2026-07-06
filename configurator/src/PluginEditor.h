// exoPALM configurator -- phase-4 editor: port picker, connection state,
// dump/save/revert, decoded parameter log. The mockup UI lands in phase 5
// on top of this working plumbing.
#pragma once
#include "PluginProcessor.h"

class ExoPalmEditor : public juce::AudioProcessorEditor,
                      private PalmDevice::Listener,
                      private juce::Timer {
public:
    explicit ExoPalmEditor(ExoPalmProcessor&);
    ~ExoPalmEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void deviceInfoReceived(const palm::DeviceInfo&) override;
    void configReceived(const palm::Config&) override;
    void connectionChanged() override;
    void midiActivity(const juce::MidiMessage&) override;
    void timerCallback() override;

    void rescanPorts();
    void logLine(const juce::String&);

    ExoPalmProcessor& proc;
    juce::ComboBox portBox;
    juce::TextButton dumpButton { "dump" }, saveButton { "save" }, revertButton { "revert" };
    juce::Label statusLabel;
    juce::TextEditor log;
    juce::Array<PalmDevice::Port> ports;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExoPalmEditor)
};
