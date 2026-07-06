#include "PluginEditor.h"

juce::AudioProcessorEditor* ExoPalmProcessor::createEditor() {
    return new ExoPalmEditor(*this);
}

ExoPalmEditor::ExoPalmEditor(ExoPalmProcessor& p)
    : juce::AudioProcessorEditor(p), proc(p) {
    setSize(560, 400);

    addAndMakeVisible(portBox);
    portBox.setTextWhenNothingSelected("midi port...");
    portBox.onChange = [this] {
        int idx = portBox.getSelectedItemIndex();
        if (idx >= 0 && idx < ports.size()) {
            if (proc.device.open(ports.getReference(idx))) {
                logLine("opened " + ports.getReference(idx).name);
                proc.device.requestInfo();
                proc.device.requestDump();
            } else {
                logLine("failed to open port");
            }
        }
    };

    addAndMakeVisible(dumpButton);
    dumpButton.onClick = [this] { proc.device.requestDump(); };
    addAndMakeVisible(saveButton);
    saveButton.onClick = [this] { proc.device.saveToDevice(); logLine("save sent"); };
    addAndMakeVisible(revertButton);
    revertButton.onClick = [this] { proc.device.revert(); proc.device.requestDump(); };

    addAndMakeVisible(statusLabel);
    statusLabel.setText("not connected", juce::dontSendNotification);

    addAndMakeVisible(log);
    log.setMultiLine(true);
    log.setReadOnly(true);
    log.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, 0));

    proc.device.setListener(this);
    rescanPorts();
    startTimer(2000);
}

ExoPalmEditor::~ExoPalmEditor() {
    proc.device.setListener(nullptr);
}

void ExoPalmEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);
}

void ExoPalmEditor::resized() {
    auto r = getLocalBounds().reduced(8);
    auto top = r.removeFromTop(26);
    portBox.setBounds(top.removeFromLeft(240));
    top.removeFromLeft(6);
    dumpButton.setBounds(top.removeFromLeft(60));
    top.removeFromLeft(4);
    saveButton.setBounds(top.removeFromLeft(60));
    top.removeFromLeft(4);
    revertButton.setBounds(top.removeFromLeft(60));
    r.removeFromTop(4);
    statusLabel.setBounds(r.removeFromTop(20));
    r.removeFromTop(4);
    log.setBounds(r);
}

void ExoPalmEditor::rescanPorts() {
    auto fresh = proc.device.scanPorts();
    if (fresh.size() == ports.size()) {
        bool same = true;
        for (int i = 0; i < fresh.size(); i++)
            if (fresh.getReference(i).name != ports.getReference(i).name) same = false;
        if (same) return;
    }
    ports = fresh;
    portBox.clear(juce::dontSendNotification);
    int id = 1;
    for (auto& p : ports) portBox.addItem(p.name, id++);
}

void ExoPalmEditor::timerCallback() { rescanPorts(); }

void ExoPalmEditor::deviceInfoReceived(const palm::DeviceInfo& i) {
    statusLabel.setText("connected: " + juce::String(i.name)
                        + "  (schema " + juce::String(i.schemaVersion)
                        + ", fw " + juce::String(i.fwVersion) + ")",
                        juce::dontSendNotification);
}

void ExoPalmEditor::configReceived(const palm::Config& c) {
    proc.lastConfig = c;
    juce::String s;
    s << "config received (" << palm::kBlobSize << " bytes)\n";
    s << "  leftHand=" << (int)c.get(palm::kAddrLeftHand)
      << "  ccChannel=" << (int)c.get(palm::kAddrCcChannel) << "\n";
    for (int p = 0; p < palm::kNumPresets; p++)
        s << "  preset " << p << ": mode=" << (int)c.get(palm::addrPreset(p, 0))
          << " selCC=" << (int)c.get(palm::addrPreset(p, 1))
          << " gW=" << (int)c.get(palm::addrPreset(p, 2))
          << "/" << (int)c.get(palm::addrPreset(p, 3))
          << " gE=" << (int)c.get(palm::addrPreset(p, 4))
          << "/" << (int)c.get(palm::addrPreset(p, 5)) << "\n";
    logLine(s.trimEnd());
}

void ExoPalmEditor::connectionChanged() {
    if (!proc.device.isOpen())
        statusLabel.setText("not connected", juce::dontSendNotification);
}

void ExoPalmEditor::midiActivity(const juce::MidiMessage& m) {
    if (m.isController() || m.isNoteOnOrOff())
        logLine(m.getDescription());
}

void ExoPalmEditor::logLine(const juce::String& s) {
    log.moveCaretToEnd();
    log.insertTextAtCaret(s + "\n");
}
