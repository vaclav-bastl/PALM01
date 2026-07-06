#include "PluginEditor.h"
#include <cmath>

juce::AudioProcessorEditor* ExoPalmProcessor::createEditor() {
    return new ExoPalmEditor(*this);
}

static const juce::Colour kBg { 0xff0d0b0d };
static const juce::Colour kFg = juce::Colours::white;
static const juce::Colour kGrey { 0xff6a6a6a };

// mockup geometry (1/2 of the 2094x1132 png)
static constexpr int kRowX = 122, kRowW = 250;   // labels at 138, values at 228
static constexpr int kColMsg = 138, kColCh = 229, kColNum = 307, kColVal = 374;

// ---------- look and feel ----------

PalmLookAndFeel::PalmLookAndFeel() {
    setColour(juce::PopupMenu::backgroundColourId, kBg);
    setColour(juce::PopupMenu::textColourId, kFg);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff2c2c2c));
    setColour(juce::PopupMenu::highlightedTextColourId, kFg);
}

void PalmLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(0, 0, box.getWidth(), box.getHeight());
    label.setBorderSize({ 0, 0, 0, 0 });
    label.setFont(font);
}

void PalmLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool, bool) {
    g.setFont(font);
    g.setColour(b.getToggleState() ? kFg : kGrey);
    g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centredLeft);
}

// ---------- ParamRow ----------

void ParamRow::paint(juce::Graphics& g) {
    auto col = isOff() ? kGrey : kFg;
    g.setFont(font);

    if (disableable) {
        // MIDI DIN plug in the gutter: filled circle, five pin holes on an arc
        float d = 11.0f, y0 = (getHeight() - d) * 0.5f;
        g.setColour(col);
        g.fillEllipse(0.0f, y0, d, d);
        g.setColour(kBg);
        float cx = d / 2, cy = y0 + d / 2, pr = d * 0.30f;
        for (float a : { -1.6f, -0.8f, 0.0f, 0.8f, 1.6f })
            g.fillEllipse(cx + pr * std::sin(a) - 1.0f, cy - pr * std::cos(a) - 1.0f, 2.0f, 2.0f);
    }

    g.setColour(col);
    g.drawText(label + ":", 16, 0, valueX - 18, getHeight(), juce::Justification::centredLeft);

    juce::String v = isOff() ? "off"
                   : (formatter ? formatter(raw) : juce::String((int)raw));
    g.drawText(v, valueX, 0, getWidth() - valueX, getHeight(), juce::Justification::centredLeft);
}

void ParamRow::mouseDown(const juce::MouseEvent& e) {
    if (disableable && e.x < 16) {
        if (isOff()) apply(lastOn);
        else { lastOn = raw; apply(255); }
        return;
    }
    dragStartVal = isOff() ? -1 : raw;
}

void ParamRow::mouseDrag(const juce::MouseEvent& e) {
    if (dragStartVal < 0) return;  // off: plug icon first
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
    eightgonBig = juce::Font(juce::FontOptions(tf)).withHeight(19.0f);
    lnf.font = eightgon;
    setLookAndFeel(&lnf);

    setSize(524, 566);  // mockup panel at 1:1

    auto styleLabel = [&](juce::Label& l, juce::Colour c = kFg) {
        addAndMakeVisible(l);
        l.setFont(eightgon);
        l.setBorderSize({ 0, 0, 0, 0 });
        l.setColour(juce::Label::textColourId, c);
        l.setJustificationType(juce::Justification::centredLeft);
    };

    addAndMakeVisible(portBox);
    portBox.setColour(juce::ComboBox::backgroundColourId, kBg);
    portBox.setColour(juce::ComboBox::textColourId, kFg);
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

    styleLabel(opLabel);   opLabel.setText("normal operation", juce::dontSendNotification);
    styleLabel(nameLabel); nameLabel.setText("name: -", juce::dontSendNotification);
    styleLabel(connLabel); connLabel.setText("connection: -", juce::dontSendNotification);

    nameLabel.setEditable(false, true);  // double-click to rename
    nameLabel.onTextChange = [this] {
        auto n = nameLabel.getText().fromFirstOccurrenceOf(":", false, false).trim();
        if (n.isNotEmpty() && proc.device.isOpen()) {
            proc.device.setName(n);
            opLabel.setText("name set - save + reboot device", juce::dontSendNotification);
        }
    };

    addAndMakeVisible(editOnly);
    editOnly.onClick = [this] {
        opLabel.setColour(juce::Label::textColourId,
                          editOnly.getToggleState() ? kGrey : kFg);
    };

    addAndMakeVisible(saveBtn);
    saveBtn.setColour(juce::TextButton::textColourOffId, kFg);
    addAndMakeVisible(revertBtn);
    revertBtn.setColour(juce::TextButton::textColourOffId, kGrey);
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
    const char* fingers[] = { "little a", "little b", "ring a", "ring b",
                              "middle a", "middle b", "index a", "index b" };
    for (int i = 0; i < 8; i++) fingerBox.addItem(fingers[i], i + 1);
    fingerBox.setSelectedItemIndex(finger, juce::dontSendNotification);
    fingerBox.onChange = [this] { finger = fingerBox.getSelectedItemIndex(); refreshFromConfig(); };

    styleLabel(touchHeader);
    touchHeader.setText("touch activated:", juce::dontSendNotification);

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

    initRow(chordRow, "chord", false, 0, 0);   // fixed mapping for now
    chordRow.formatter = [](uint8_t) { return juce::String("c major"); };

    styleLabel(handLabel);
    handLabel.setJustificationType(juce::Justification::centred);
    handLabel.setInterceptsMouseClicks(false, false);  // handArea handles clicks

    proc.device.setListener(this);
    rescanPorts();
    refreshFromConfig();
    startTimer(2000);

    if (proc.device.isOpen()) proc.device.requestDump();
}

ExoPalmEditor::~ExoPalmEditor() {
    setLookAndFeel(nullptr);
    proc.device.setListener(nullptr);
}

// full-height device silhouette: cut-corner outline, sensor cluster, four
// finger pad pairs (selected = filled), button cluster, logo -- per mockup
void ExoPalmEditor::drawDevice(juce::Graphics& g) {
    g.setColour(kFg);
    const float x0 = 19, y0 = 59, x1 = 116, y1 = 553, c = 16;
    juce::Path body;
    body.startNewSubPath(x0 + c, y0); body.lineTo(x1 - c, y0); body.lineTo(x1, y0 + c);
    body.lineTo(x1, y1 - c); body.lineTo(x1 - c, y1); body.lineTo(x0 + c, y1);
    body.lineTo(x0, y1 - c); body.lineTo(x0, y0 + c);
    body.closeSubPath();
    g.strokePath(body, juce::PathStrokeType(1.2f));

    // sensor cluster: filled hexagon + two ringed octagons
    juce::Path hex;
    hex.addPolygon({ 38.0f, 84.0f }, 6, 15.0f, juce::MathConstants<float>::halfPi);
    g.fillPath(hex);
    for (float cx : { 70.0f, 99.0f }) {
        juce::Path oct;
        oct.addPolygon({ cx, 84.0f }, 8, 13.0f, juce::MathConstants<float>::pi / 8.0f);
        g.strokePath(oct, juce::PathStrokeType(1.2f));
        g.drawEllipse(cx - 5.5f, 78.5f, 11.0f, 11.0f, 1.2f);
    }

    // finger pads: rows top->bottom index/middle/ring/little, columns a/b
    static constexpr int rowBase[4] = { 6, 4, 2, 0 };
    for (int r = 0; r < 4; r++)
        for (int col = 0; col < 2; col++) {
            auto& rect = padRects[r * 2 + col];
            juce::Path oct;
            oct.addPolygon(rect.getCentre().toFloat(), 8, 17.5f,
                           juce::MathConstants<float>::pi / 8.0f);
            if (!noteMode() && finger == rowBase[r] + col) g.fillPath(oct);
            else g.strokePath(oct, juce::PathStrokeType(1.3f));
        }

    // button cluster
    for (int i = 0; i < 3; i++) {
        auto sq = juce::Rectangle<float>(26.0f + i * 30.0f, 440.0f, 22.0f, 22.0f);
        g.drawRect(sq, 1.2f);
        if (i == 1) {
            juce::Path h2;
            h2.addPolygon(sq.getCentre(), 6, 6.0f, juce::MathConstants<float>::halfPi);
            g.strokePath(h2, juce::PathStrokeType(1.2f));
        } else
            g.drawEllipse(sq.reduced(5.0f), 1.2f);
    }
    auto sq = juce::Rectangle<float>(56.0f, 468.0f, 22.0f, 22.0f);
    g.drawRect(sq, 1.2f);
    g.drawEllipse(sq.reduced(5.0f), 1.2f);

    g.setFont(eightgonBig);
    g.drawText("EXO", 30, 500, 70, 19, juce::Justification::left);
    g.drawText("PALM", 30, 519, 70, 19, juce::Justification::left);
}

// line-drawn hand, fingers up, strap across the palm; mirrored for right
void ExoPalmEditor::drawHand(juce::Graphics& g) {
    bool left = cfg().get(palm::kAddrLeftHand) != 0;

    juce::Path h;
    h.startNewSubPath(22, 110);
    h.lineTo(16, 80);
    h.lineTo(3, 68); h.lineTo(7, 59); h.lineTo(17, 67);    // thumb
    h.lineTo(16, 52);
    h.lineTo(17, 14); h.lineTo(23, 12); h.lineTo(25, 48);  // index
    h.lineTo(27, 8);  h.lineTo(33, 6);  h.lineTo(35, 46);  // middle
    h.lineTo(37, 5);  h.lineTo(43, 7);  h.lineTo(45, 48);  // ring
    h.lineTo(47, 16); h.lineTo(53, 18); h.lineTo(54, 52);  // little
    h.lineTo(58, 80); h.lineTo(52, 110);
    h.closeSubPath();

    juce::Path strap;
    strap.addRoundedRectangle(-29.0f, -4.5f, 58.0f, 9.0f, 4.5f);
    strap.applyTransform(juce::AffineTransform::rotation(-0.14f).translated(35.0f, 60.0f));

    auto place = juce::AffineTransform::translation(322.0f, 96.0f);
    if (!left)
        place = juce::AffineTransform::scale(-1.0f, 1.0f).translated(60.0f, 0.0f)
                    .followedBy(place);

    g.setColour(kFg);
    g.strokePath(h, juce::PathStrokeType(1.5f), place);
    juce::Path s2(strap); s2.applyTransform(place);
    g.setColour(kBg); g.fillPath(s2);          // strap covers the palm lines
    g.setColour(kFg);
    juce::Path s3(strap); s3.applyTransform(place);
    g.fillPath(s3);
}

// wifi fan + bluetooth rune after "connection:", active link in white
void ExoPalmEditor::drawConnIcons(juce::Graphics& g) {
    bool open = proc.device.isOpen() || connLabel.getText().contains("hub")
                                     || connLabel.getText().contains("bt");
    bool bt = connLabel.getText().contains("bt");

    float wx = 438.0f, wy = 41.0f;
    g.setColour(open && !bt ? kFg : kGrey);
    g.fillEllipse(wx - 1.5f, wy - 1.5f, 3.0f, 3.0f);
    juce::Path arcs;
    arcs.addCentredArc(wx, wy, 5.0f, 5.0f, 0, -0.7f, 0.7f, true);
    arcs.addCentredArc(wx, wy, 9.0f, 9.0f, 0, -0.7f, 0.7f, true);
    g.strokePath(arcs, juce::PathStrokeType(1.4f));

    g.setColour(open && bt ? kFg : kGrey);
    juce::Path b;
    b.startNewSubPath(455.0f, 33.0f);
    b.lineTo(463.0f, 40.0f); b.lineTo(459.0f, 43.5f); b.lineTo(459.0f, 29.5f);
    b.lineTo(463.0f, 33.0f); b.lineTo(455.0f, 40.0f);
    g.strokePath(b, juce::PathStrokeType(1.2f));
}

// note mode: two-row piano pad grid, chord tones in white
void ExoPalmEditor::drawChordGrid(juce::Graphics& g) {
    const int gx = 196, wy = 306, by = 282, sz = 23, pitch = 33;
    static const bool tone[7] = { true, false, true, false, true, false, false };  // c e g
    for (int i = 0; i < 7; i++) {
        g.setColour(tone[i] ? kFg : kGrey);
        g.fillRect(gx + i * pitch, wy, sz, sz);
    }
    g.setColour(kGrey.darker(0.35f));
    for (int i : { 0, 1, 3, 4, 5 })
        g.fillRect(gx + i * pitch + 20, by, sz, sz);
}

void ExoPalmEditor::paint(juce::Graphics& g) {
    g.fillAll(kBg);
    g.setFont(eightgon);

    drawDevice(g);
    drawHand(g);
    drawConnIcons(g);
    if (noteMode()) drawChordGrid(g);

    // midi monitor
    int my = 385;
    g.setColour(kFg);
    g.setFont(eightgon);
    g.drawText("midi monitor", kColMsg, my, 200, 16, juce::Justification::left);
    g.setColour(kGrey);
    const char* heads[] = { "message", "channel", "number", "value" };
    int colX[] = { kColMsg, kColCh, kColNum, kColVal };
    for (int i = 0; i < 4; i++)
        g.drawText(heads[i], colX[i], my + 18, 100, 16, juce::Justification::left);
    g.setColour(kFg);
    int y = my + 36;
    for (auto& row : monitor) {
        g.drawText(row.msg, colX[0], y, 90, 16, juce::Justification::left);
        g.drawText(row.ch,  colX[1], y, 60, 16, juce::Justification::left);
        g.drawText(row.num, colX[2], y, 70, 16, juce::Justification::left);
        g.drawText(row.val, colX[3], y, 60, 16, juce::Justification::left);
        y += 18;
    }
}

void ExoPalmEditor::resized() {
    portBox.setBounds(17, 8, 250, 16);
    opLabel.setBounds(17, 28, 122, 16);
    editOnly.setBounds(141, 28, 130, 16);
    nameLabel.setBounds(300, 8, 170, 16);
    connLabel.setBounds(300, 28, 130, 16);
    saveBtn.setBounds(408, 542, 44, 16);
    revertBtn.setBounds(456, 542, 54, 16);

    int y = 63, h = 16, pitch = 18;
    auto place = [&](juce::Component& c) { c.setBounds(kRowX, y, kRowW, h); y += pitch; };
    place(presetRow);
    place(channelRow);
    place(modeRow);
    place(gWristCc);
    place(gWristRel);
    place(gElbowCc);
    place(gElbowRel);

    touchHeader.setBounds(kRowX + 16, 220, 108, h);
    fingerBox.setBounds(kRowX + 106, 220, 110, h);
    y = 238;
    place(tCc);
    place(tRange);
    place(fWristCc);
    place(fWristRel);
    place(fElbowCc);
    place(fElbowRel);

    selectorRow.setBounds(kRowX, 208, kRowW, h);
    chordRow.setBounds(kRowX, 226, kRowW, h);

    handLabel.setBounds(312, 76, 90, 16);
    handArea = { 312, 74, 110, 140 };

    for (int r = 0; r < 4; r++)
        for (int col = 0; col < 2; col++)
            padRects[r * 2 + col] = { 45 + col * 45 - 17, 157 + r * 65 - 17, 35, 35 };
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
    chordRow.setVisible(nm);

    if (!nm) {
        tCc.setRaw(c.get(palm::addrFinger(finger, preset, 0)));
        tRange.setRaw(c.get(palm::addrFinger(finger, preset, 1)));
        fWristCc.setRaw(c.get(palm::addrFinger(finger, preset, 2)));
        fWristRel.setRaw(c.get(palm::addrFinger(finger, preset, 3)));
        fElbowCc.setRaw(c.get(palm::addrFinger(finger, preset, 4)));
        fElbowRel.setRaw(c.get(palm::addrFinger(finger, preset, 5)));
    } else {
        selectorRow.setRaw(c.get(palm::addrPreset(preset, 1)));
        chordRow.setRaw(0);
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
    if (e.originalComponent != this) return;

    if (handArea.contains(e.getPosition())) {
        uint8_t v = cfg().get(palm::kAddrLeftHand) ? 0 : 1;
        sendParam(palm::kAddrLeftHand, v);
        refreshFromConfig();
        return;
    }

    static constexpr int rowBase[4] = { 6, 4, 2, 0 };
    for (int i = 0; i < 8; i++)
        if (padRects[i].contains(e.getPosition())) {
            finger = rowBase[i / 2] + (i % 2);
            fingerBox.setSelectedItemIndex(finger, juce::dontSendNotification);
            refreshFromConfig();
            return;
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
    repaint();
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
    repaint(kColMsg - 4, 380, getWidth() - kColMsg, 160);
}
