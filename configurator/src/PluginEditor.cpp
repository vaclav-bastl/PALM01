#include "PluginEditor.h"
#include <deque>

juce::AudioProcessorEditor* ExoPalmProcessor::createEditor() {
    return new ExoPalmEditor(*this);
}

static const juce::Colour kBg = juce::Colours::black;
static const juce::Colour kFg = juce::Colours::white;
static const juce::Colour kGrey { 0xff6a6a6a };

// ---------- ParamRow ----------

void ParamRow::paint(juce::Graphics& g) {
    g.setFont(font.withHeight(getHeight() * 0.72f));
    auto col = isOff() ? kGrey : kFg;

    int x = 0;
    if (disableable) {
        auto dot = juce::Rectangle<float>(2.0f, getHeight() * 0.32f, getHeight() * 0.34f, getHeight() * 0.34f);
        g.setColour(col);
        if (isOff()) g.drawEllipse(dot, 1.0f);
        else g.fillEllipse(dot);
        x = getHeight() / 2 + 6;
    }
    g.setColour(col);
    g.drawText(label + ":", x, 0, getWidth() - x, getHeight(), juce::Justification::centredLeft);

    juce::String v = isOff() ? "off"
                   : (formatter ? formatter(raw) : juce::String((int)raw));
    g.drawText(v, getWidth() - 64, 0, 60, getHeight(), juce::Justification::centredLeft);
}

void ParamRow::mouseDown(const juce::MouseEvent& e) {
    if (disableable && e.x < getHeight() / 2 + 4) {
        if (isOff()) apply(lastOn);
        else { lastOn = raw; apply(255); }
        return;
    }
    dragStartVal = isOff() ? -1 : raw;
}

void ParamRow::mouseDrag(const juce::MouseEvent& e) {
    if (dragStartVal < 0) return;  // off: dot first
    int v = dragStartVal + (-e.getDistanceFromDragStartY() / 3);
    v = juce::jlimit((int)minV, (int)maxV, v);
    if ((uint8_t)v != raw) apply((uint8_t)v);
}

// ---------- editor ----------

ExoPalmEditor::ExoPalmEditor(ExoPalmProcessor& p)
    : juce::AudioProcessorEditor(p), proc(p) {
    auto tf = juce::Typeface::createSystemTypefaceFor(BinaryData::EightgonOGn6p_ttf,
                                                      (size_t)BinaryData::EightgonOGn6p_ttfSize);
    eightgon = juce::Font(juce::FontOptions(tf)).withHeight(13.0f);
    eightgonBig = juce::Font(juce::FontOptions(tf)).withHeight(17.0f);

    setSize(540, 560);

    auto styleLabel = [&](juce::Label& l, juce::Colour c = kFg) {
        addAndMakeVisible(l);
        l.setFont(eightgon);
        l.setColour(juce::Label::textColourId, c);
        l.setJustificationType(juce::Justification::centredLeft);
    };

    addAndMakeVisible(portBox);
    portBox.setColour(juce::ComboBox::backgroundColourId, kBg);
    portBox.setColour(juce::ComboBox::textColourId, kFg);
    portBox.setColour(juce::ComboBox::outlineColourId, kGrey);
    portBox.setColour(juce::ComboBox::arrowColourId, kFg);
    portBox.setTextWhenNothingSelected("midi port: ...");
    portBox.onChange = [this] {
        int idx = portBox.getSelectedItemIndex();
        if (idx >= 0 && idx < ports.size() && proc.device.open(ports.getReference(idx))) {
            proc.device.requestInfo();
            proc.device.requestDump();
            connLabel.setText(juce::String("connection: ")
                              + (ports.getReference(idx).isBluetooth ? "bt" : "hub"),
                              juce::dontSendNotification);
        }
    };

    styleLabel(opLabel);      opLabel.setText("normal operation", juce::dontSendNotification);
    styleLabel(nameLabel);    nameLabel.setText("name: -", juce::dontSendNotification);
    styleLabel(connLabel, kGrey); connLabel.setText("connection: -", juce::dontSendNotification);

    nameLabel.setEditable(false, true);  // double-click to rename
    nameLabel.onTextChange = [this] {
        auto n = nameLabel.getText().fromFirstOccurrenceOf(":", false, false).trim();
        if (n.isNotEmpty() && proc.device.isOpen()) {
            proc.device.setName(n);
            opLabel.setText("name set - save + reboot device", juce::dontSendNotification);
        }
    };

    addAndMakeVisible(editOnly);
    editOnly.setColour(juce::ToggleButton::textColourId, kGrey);
    editOnly.setColour(juce::ToggleButton::tickColourId, kFg);
    editOnly.setColour(juce::ToggleButton::tickDisabledColourId, kGrey);

    for (auto* b : { &saveBtn, &revertBtn }) {
        addAndMakeVisible(*b);
        b->setColour(juce::TextButton::buttonColourId, kBg);
        b->setColour(juce::TextButton::textColourOffId, kFg);
    }
    saveBtn.onClick = [this] {
        for (auto& [addr, v] : dirty) proc.device.setParam(addr, v);
        dirty.clear();
        proc.device.saveToDevice();
        opLabel.setText("saved to device", juce::dontSendNotification);
    };
    revertBtn.onClick = [this] {
        dirty.clear();
        proc.device.revert();
        proc.device.requestDump();
        opLabel.setText("reverted", juce::dontSendNotification);
    };

    // ---- rows ----
    auto initRow = [&](ParamRow& r, const juce::String& label, bool disableable,
                       uint8_t minV, uint8_t maxV) {
        addAndMakeVisible(r);
        r.font = eightgon;
        r.label = label;
        r.disableable = disableable;
        r.minV = minV; r.maxV = maxV;
    };

    initRow(presetRow, "preset", false, 0, 3);
    presetRow.onValue = [this](uint8_t v) { preset = v; refreshFromConfig(); };

    initRow(channelRow, "channel", false, 1, 16);
    channelRow.onValue = [this](uint8_t v) {
        sendParam(noteMode() ? palm::addrPreset(preset, 0) : palm::kAddrCcChannel, v);
    };

    initRow(modeRow, "mode", false, 0, 1);
    modeRow.formatter = [](uint8_t v) { return v ? "note" : "cc"; };
    modeRow.onValue = [this](uint8_t v) {
        sendParam(palm::addrPreset(preset, 0), v ? (uint8_t)1 : (uint8_t)0);
        refreshFromConfig();
    };

    initRow(gWristCc, "wrist cc", true, 0, 127);
    gWristCc.onValue = [this](uint8_t v) { sendParam(palm::addrPreset(preset, 2), v); refreshFromConfig(); };
    initRow(gWristRel, "on release", true, 0, 127);
    gWristRel.formatter = [](uint8_t v) { return v == 255 ? "hang" : juce::String((int)v); };
    gWristRel.onValue = [this](uint8_t v) { sendParam(palm::addrPreset(preset, 3), v); };
    initRow(gElbowCc, "elbow cc", true, 0, 127);
    gElbowCc.onValue = [this](uint8_t v) { sendParam(palm::addrPreset(preset, 4), v); refreshFromConfig(); };
    initRow(gElbowRel, "on release", true, 0, 127);
    gElbowRel.formatter = gWristRel.formatter;
    gElbowRel.onValue = [this](uint8_t v) { sendParam(palm::addrPreset(preset, 5), v); };

    addAndMakeVisible(fingerBox);
    fingerBox.setColour(juce::ComboBox::backgroundColourId, kBg);
    fingerBox.setColour(juce::ComboBox::textColourId, kFg);
    fingerBox.setColour(juce::ComboBox::outlineColourId, kGrey);
    fingerBox.setColour(juce::ComboBox::arrowColourId, kFg);
    const char* fingers[] = { "little a", "little b", "ring a", "ring b",
                              "middle a", "middle b", "index a", "index b" };
    for (int i = 0; i < 8; i++) fingerBox.addItem(fingers[i], i + 1);
    fingerBox.setSelectedItemIndex(finger, juce::dontSendNotification);
    fingerBox.onChange = [this] { finger = fingerBox.getSelectedItemIndex(); refreshFromConfig(); };

    styleLabel(touchHeader);
    touchHeader.setText("touch activated", juce::dontSendNotification);

    initRow(tCc, "touch cc", true, 0, 127);
    tCc.onValue = [this](uint8_t v) { sendParam(palm::addrFinger(finger, preset, 0), v); refreshFromConfig(); };
    initRow(tRange, "range", true, 0, 127);
    tRange.formatter = [](uint8_t v) { return v == 255 ? "gate" : juce::String((int)v); };
    tRange.onValue = [this](uint8_t v) { sendParam(palm::addrFinger(finger, preset, 1), v); };
    initRow(fWristCc, "wrist cc", true, 0, 127);
    fWristCc.onValue = [this](uint8_t v) { sendParam(palm::addrFinger(finger, preset, 2), v); refreshFromConfig(); };
    initRow(fWristRel, "on release", true, 0, 127);
    fWristRel.formatter = gWristRel.formatter;
    fWristRel.onValue = [this](uint8_t v) { sendParam(palm::addrFinger(finger, preset, 3), v); };
    initRow(fElbowCc, "elbow cc", true, 0, 127);
    fElbowCc.onValue = [this](uint8_t v) { sendParam(palm::addrFinger(finger, preset, 4), v); refreshFromConfig(); };
    initRow(fElbowRel, "on release", true, 0, 127);
    fElbowRel.formatter = gWristRel.formatter;
    fElbowRel.onValue = [this](uint8_t v) { sendParam(palm::addrFinger(finger, preset, 5), v); };

    initRow(selectorRow, "selector cc", true, 0, 127);
    selectorRow.onValue = [this](uint8_t v) { sendParam(palm::addrPreset(preset, 1), v); };

    styleLabel(chordLabel, kGrey);
    chordLabel.setText("chord:  c major", juce::dontSendNotification);

    styleLabel(handLabel);
    handLabel.setInterceptsMouseClicks(true, false);
    handLabel.addMouseListener(this, false);

    proc.device.setListener(this);
    rescanPorts();
    refreshFromConfig();
    startTimer(2000);

    if (proc.device.isOpen()) proc.device.requestDump();
}

ExoPalmEditor::~ExoPalmEditor() {
    proc.device.setListener(nullptr);
}

void ExoPalmEditor::paint(juce::Graphics& g) {
    g.fillAll(kBg);
    g.setColour(kFg);
    g.setFont(eightgon);



    // left sidebar: stylized device (octagon pads + outline)
    auto side = juce::Rectangle<int>(10, 66, 96, 300);
    g.drawRect(side.expanded(4), 1);
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 2; c++) {
            juce::Path oct;
            float cx = (float)(side.getX() + 24 + c * 44);
            float cy = (float)(side.getY() + 28 + r * 46);
            oct.addPolygon({ cx, cy }, 8, 16.0f, juce::MathConstants<float>::pi / 8.0f);
            g.strokePath(oct, juce::PathStrokeType(1.2f));
        }
    // thumb pads
    for (int t = 0; t < 3; t++) {
        juce::Path oct;
        oct.addPolygon({ (float)(side.getX() + 16 + t * 32), (float)(side.getBottom() - 24) },
                       8, 10.0f, juce::MathConstants<float>::pi / 8.0f);
        g.strokePath(oct, juce::PathStrokeType(1.0f));
    }

    // logo
    g.setFont(eightgonBig);
    g.drawText("EXO", 14, getHeight() - 78, 60, 20, juce::Justification::left);
    g.drawText("PALM", 14, getHeight() - 58, 60, 20, juce::Justification::left);
    g.drawRect(10, getHeight() - 84, 68, 52, 1);

    // header separators
    g.setColour(kGrey.withAlpha(0.4f));
    g.drawHorizontalLine(58, 8.0f, (float)(getWidth() - 8));

    // NOTE mode: decorative pad squares (as in the mockup)
    if (noteMode()) {
        int sx = 130, sy = 306;
        for (int i = 0; i < 8; i++) {
            auto r = juce::Rectangle<int>(sx + i * 22, sy, 14, 14);
            g.setColour(i % 3 == 0 ? kFg : kGrey);
            if (i % 2 == 0) g.fillRect(r); else g.drawRect(r, 1);
        }
    }

    // midi monitor
    int my = 380;
    g.setColour(kFg);
    g.setFont(eightgon);
    g.drawText("midi monitor", 130, my, 200, 16, juce::Justification::left);
    g.setColour(kGrey);
    const char* heads[] = { "message", "channel", "number", "value" };
    int colX[] = { 130, 280, 360, 440 };
    for (int i = 0; i < 4; i++)
        g.drawText(heads[i], colX[i], my + 20, 100, 14, juce::Justification::left);
    g.setColour(kFg);
    int y = my + 38;
    for (auto& row : monitor) {
        g.drawText(row.msg, colX[0], y, 140, 14, juce::Justification::left);
        g.drawText(row.ch,  colX[1], y, 60, 14, juce::Justification::left);
        g.drawText(row.num, colX[2], y, 70, 14, juce::Justification::left);
        g.drawText(row.val, colX[3], y, 60, 14, juce::Justification::left);
        y += 16;
    }
}

void ExoPalmEditor::resized() {
    portBox.setBounds(10, 8, 200, 20);
    nameLabel.setBounds(getWidth() - 220, 8, 150, 20);
    connLabel.setBounds(getWidth() - 220, 30, 150, 20);
    opLabel.setBounds(10, 32, 160, 20);
    editOnly.setBounds(170, 32, 150, 20);
    saveBtn.setBounds(getWidth() - 66, 8, 56, 20);
    revertBtn.setBounds(getWidth() - 66, 30, 56, 20);

    int x = 130, w = 250, h = 18, y = 70;
    auto place = [&](juce::Component& c, int gap = 2) { c.setBounds(x, y, w, h); y += h + gap; };
    place(presetRow);
    place(channelRow);
    place(modeRow, 10);
    place(gWristCc);
    place(gWristRel, 2);
    place(gElbowCc);
    place(gElbowRel, 14);

    handLabel.setBounds(x + w + 10, 70, 120, 60);

    fingerBox.setBounds(x + 110, y - 2, 120, 20);
    touchHeader.setBounds(x, y, 110, h); y += h + 4;
    place(tCc);
    place(tRange, 2);
    place(fWristCc);
    place(fWristRel, 2);
    place(fElbowCc);
    place(fElbowRel);

    selectorRow.setBounds(x, 210, w, h);
    chordLabel.setBounds(x, 232, w, h);
}

void ExoPalmEditor::refreshFromConfig() {
    loadingUi = true;
    auto& c = cfg();
    bool nm = noteMode();

    presetRow.setRaw((uint8_t)preset);
    modeRow.setRaw(nm ? 1 : 0);
    channelRow.setRaw(nm ? c.get(palm::addrPreset(preset, 0)) : c.get(palm::kAddrCcChannel));

    gWristCc.setRaw(c.get(palm::addrPreset(preset, 2)));
    gWristRel.setRaw(c.get(palm::addrPreset(preset, 3)));
    gElbowCc.setRaw(c.get(palm::addrPreset(preset, 4)));
    gElbowRel.setRaw(c.get(palm::addrPreset(preset, 5)));
    // release rows grey out alongside their cc
    gWristRel.setEnabled(!gWristCc.isOff());
    gElbowRel.setEnabled(!gElbowCc.isOff());

    for (auto* r : { &tCc, &tRange, &fWristCc, &fWristRel, &fElbowCc, &fElbowRel })
        r->setVisible(!nm);
    fingerBox.setVisible(!nm);
    touchHeader.setVisible(!nm);
    selectorRow.setVisible(nm);
    chordLabel.setVisible(nm);

    if (!nm) {
        tCc.setRaw(c.get(palm::addrFinger(finger, preset, 0)));
        tRange.setRaw(c.get(palm::addrFinger(finger, preset, 1)));
        fWristCc.setRaw(c.get(palm::addrFinger(finger, preset, 2)));
        fWristRel.setRaw(c.get(palm::addrFinger(finger, preset, 3)));
        fElbowCc.setRaw(c.get(palm::addrFinger(finger, preset, 4)));
        fElbowRel.setRaw(c.get(palm::addrFinger(finger, preset, 5)));
    } else {
        selectorRow.setRaw(c.get(palm::addrPreset(preset, 1)));
    }

    handLabel.setText(c.get(palm::kAddrLeftHand) ? "left hand" : "right hand",
                      juce::dontSendNotification);
    loadingUi = false;
    repaint();
}

void ExoPalmEditor::sendParam(int addr, uint8_t v) {
    if (loadingUi) return;
    cfg().set(addr, v);
    if (editOnly.getToggleState()) dirty[addr] = v;
    else proc.device.setParam(addr, v);
}

void ExoPalmEditor::rescanPorts() {
    auto fresh = proc.device.scanPorts();
    bool same = fresh.size() == ports.size();
    if (same)
        for (int i = 0; i < fresh.size(); i++)
            if (fresh.getReference(i).name != ports.getReference(i).name) same = false;
    if (same) return;
    ports = fresh;
    portBox.clear(juce::dontSendNotification);
    int id = 1;
    for (auto& p : ports) portBox.addItem("midi port: " + p.name, id++);
}

void ExoPalmEditor::timerCallback() { rescanPorts(); }

void ExoPalmEditor::mouseDown(const juce::MouseEvent& e) {
    if (e.originalComponent == &handLabel) {
        uint8_t v = cfg().get(palm::kAddrLeftHand) ? 0 : 1;
        sendParam(palm::kAddrLeftHand, v);
        refreshFromConfig();
    }
}

void ExoPalmEditor::deviceInfoReceived(const palm::DeviceInfo& i) {
    nameLabel.setText("name: " + juce::String(i.name), juce::dontSendNotification);
    opLabel.setText("normal operation", juce::dontSendNotification);
}

void ExoPalmEditor::configReceived(const palm::Config& c) {
    proc.lastConfig = c;
    refreshFromConfig();
}

void ExoPalmEditor::connectionChanged() {
    if (!proc.device.isOpen()) {
        connLabel.setText("connection: -", juce::dontSendNotification);
        opLabel.setText("not connected", juce::dontSendNotification);
    }
}

void ExoPalmEditor::midiActivity(const juce::MidiMessage& m) {
    MonRow r;
    if (m.isController()) { r.msg = "cc"; r.num = juce::String(m.getControllerNumber()); r.val = juce::String(m.getControllerValue()); }
    else if (m.isNoteOn()) { r.msg = "note on"; r.num = juce::MidiMessage::getMidiNoteName(m.getNoteNumber(), true, true, 3); r.val = juce::String((int)m.getVelocity()); }
    else if (m.isNoteOff()) { r.msg = "note off"; r.num = juce::MidiMessage::getMidiNoteName(m.getNoteNumber(), true, true, 3); r.val = "0"; }
    else return;
    r.ch = juce::String(m.getChannel());
    monitor.push_front(r);
    while (monitor.size() > 6) monitor.pop_back();
    repaint(120, 380, getWidth() - 130, 160);
}
