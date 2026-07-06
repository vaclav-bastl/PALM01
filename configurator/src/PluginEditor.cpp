#include "PluginEditor.h"
#include <cmath>

juce::AudioProcessorEditor* ExoPalmProcessor::createEditor() {
    return new ExoPalmEditor(*this);
}

static const juce::Colour kBg { 0xff0d0b0d };
static const juce::Colour kFg = juce::Colours::white;
static const juce::Colour kGrey { 0xff6a6a6a };
static const juce::Colour kDark { 0xff3f3f3f };

// mockup geometry (1/2 of the 2094x1132 png)
static constexpr int kRowX = 122, kRowW = 250;   // labels at 138, values at 228
static constexpr int kColMsg = 138, kColCh = 229, kColNum = 307, kColVal = 374;

// chord grid: one octave, 7 white + 5 black keys
static constexpr int kGridX = 196, kGridWhiteY = 306, kGridBlackY = 282;
static constexpr int kKey = 23, kKeyPitch = 33;
static const int kWhitePcs[7] = { 0, 2, 4, 5, 7, 9, 11 };
static const int kBlackSlots[5] = { 0, 1, 3, 4, 5 };   // gaps carrying a black key
static const int kBlackPcs[5] = { 1, 3, 6, 8, 10 };

// note-mode root fingers per hand (mirrors firmware getRootFromTouchesLocal)
static const int kRootFingersLeft[7]  = { 6, 4, 2, 0, 7, 5, 3 };  // c d e f g a b
static const int kRootFingersRight[7] = { 7, 5, 3, 1, 6, 4, 2 };

static juce::Rectangle<int> whiteKeyRect(int i) {
    return { kGridX + i * kKeyPitch, kGridWhiteY, kKey, kKey };
}
static juce::Rectangle<int> blackKeyRect(int j) {
    return { kGridX + kBlackSlots[j] * kKeyPitch + 20, kGridBlackY, kKey, kKey };
}

static juce::String chordName(int rootPc, uint16_t mask) {
    static const char* notes[12] = { "c", "c#", "d", "d#", "e", "f",
                                     "f#", "g", "g#", "a", "a#", "b" };
    struct Q { uint16_t rel; const char* n; };
    static const Q quals[] = {
        { 0b000010010001, "major" }, { 0b000010001001, "minor" },
        { 0b000001001001, "dim" },   { 0b000100010001, "aug" },
        { 0b010010010001, "7" },     { 0b010010001001, "m7" },
        { 0b100010010001, "maj7" },  { 0b000010000101, "sus2" },
        { 0b000010100001, "sus4" },
    };
    uint16_t rel = 0;
    for (int pc = 0; pc < 12; pc++)
        if (mask & (1u << pc)) rel |= (uint16_t)(1u << ((pc - rootPc + 12) % 12));
    for (auto& q : quals)
        if (rel == q.rel) return juce::String(notes[rootPc]) + " " + q.n;
    return juce::String(notes[rootPc]) + " custom";
}

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
    auto col = (isOff() || !isEnabled()) ? kGrey : kFg;
    g.setFont(font);

    if (hasIcon) {
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

    juce::String v;
    if (isOff()) v = "off";
    else if (raw == 255 && formatter) v = formatter(raw);
    else v = formatter ? formatter(raw) : juce::String((int)raw);
    g.drawText(v, valueX, 0, getWidth() - valueX, getHeight(), juce::Justification::centredLeft);
}

void ParamRow::mouseDown(const juce::MouseEvent& e) {
    if (readOnly) return;
    grabKeyboardFocus();  // closes any text box open elsewhere
    if (hasIcon && e.x < 16) {
        // send the mapped MIDI message and open the value box for typing
        if (onIconClick && !isOff()) onIconClick();
        openEdit();
        return;
    }
    if (toggleOnClick) { apply(raw ? (uint8_t)0 : (uint8_t)1); return; }
    dragStartVal = raw == 255 ? (disableable ? (int)minV - 12 : (int)maxV + 12)
                              : (int)raw;
}

void ParamRow::mouseDrag(const juce::MouseEvent& e) {
    if (readOnly || toggleOnClick || (hasIcon && e.getMouseDownX() < 16)) return;
    int v = dragStartVal + (-e.getDistanceFromDragStartY() / 3);
    uint8_t nv;
    if (disableable && v < (int)minV - 4) nv = 255;
    else if (allow255Above && v > (int)maxV + 4) nv = 255;
    else nv = (uint8_t)juce::jlimit((int)minV, (int)maxV, v);
    if (nv != raw) apply(nv);
}

void ParamRow::mouseDoubleClick(const juce::MouseEvent&) {
    if (readOnly || toggleOnClick) return;
    openEdit();
}

void ParamRow::openEdit() {
    if (!edit) {
        edit = std::make_unique<juce::TextEditor>();
        addAndMakeVisible(*edit);
        edit->setFont(font);
        edit->setJustification(juce::Justification::centredLeft);
        edit->setIndents(2, 0);
        edit->setBorder({ 0, 0, 0, 0 });
        edit->setColour(juce::TextEditor::backgroundColourId, kBg);
        edit->setColour(juce::TextEditor::textColourId, kFg);
        edit->setColour(juce::TextEditor::outlineColourId, kGrey);
        edit->setColour(juce::TextEditor::focusedOutlineColourId, kFg);
        edit->setColour(juce::TextEditor::highlightColourId, juce::Colour(0xff2c2c2c));
        edit->onReturnKey = [this] { commitEdit(true); };
        edit->onEscapeKey = [this] { commitEdit(false); };
        edit->onFocusLost = [this] { commitEdit(true); };
    }
    edit->setBounds(valueX - 3, 0, 54, getHeight());
    edit->setText(isOff() ? "off" : juce::String((int)raw), juce::dontSendNotification);
    edit->setVisible(true);
    edit->grabKeyboardFocus();
    edit->selectAll();
}

void ParamRow::commitEdit(bool applyText) {
    if (!edit || committing) return;
    committing = true;
    auto t = edit->getText().trim().toLowerCase();
    edit->setVisible(false);
    committing = false;
    if (!applyText || t.isEmpty()) return;

    if (disableable && t.startsWith("o")) { apply(255); return; }               // "off"
    if (allow255Above && (t.startsWith("h") || t.startsWith("g"))) {            // "hang"/"gate"
        apply(255); return;
    }
    if (t.containsOnly("0123456789")) {
        int v = t.getIntValue();
        if (allow255Above && v > (int)maxV) { apply(255); return; }
        apply((uint8_t)juce::jlimit((int)minV, (int)maxV, v));
    }
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

    addAndMakeVisible(canvas);
    canvas.setBounds(0, 0, kCanvasW, kCanvasH);
    canvas.onPaint = [this](juce::Graphics& g) { canvasPaint(g); };
    canvas.onMouse = [this](const juce::MouseEvent& e) { canvasMouse(e); };

    auto styleLabel = [&](juce::Label& l, juce::Colour c = kFg) {
        canvas.addAndMakeVisible(l);
        l.setFont(eightgon);
        l.setBorderSize({ 0, 0, 0, 0 });
        l.setColour(juce::Label::textColourId, c);
        l.setJustificationType(juce::Justification::centredLeft);
    };

    canvas.addAndMakeVisible(portBox);
    portBox.setColour(juce::ComboBox::backgroundColourId, kBg);
    portBox.setColour(juce::ComboBox::textColourId, kFg);
    portBox.setTextWhenNothingSelected("midi port: ...");
    portBox.onChange = [this] { openPortAt(portBox.getSelectedItemIndex()); };

    styleLabel(opLabel);   opLabel.setText("normal operation", juce::dontSendNotification);
    styleLabel(nameLabel); nameLabel.setText("name: -", juce::dontSendNotification);
    styleLabel(connLabel); connLabel.setText("connection: -", juce::dontSendNotification);

    // click "normal operation" to leave edit-only mode
    opLabel.onClick = [this] {
        if (editOnly.getToggleState()) {
            editOnly.setToggleState(false, juce::dontSendNotification);
            proc.device.setEditMode(false);
            opLabel.setColour(juce::Label::textColourId, kFg);
        }
    };

    nameLabel.setEditable(false, true);  // double-click to rename
    nameLabel.onTextChange = [this] {
        auto n = nameLabel.getText().fromFirstOccurrenceOf(":", false, false).trim();
        if (n.isNotEmpty() && proc.device.isOpen()) {
            proc.device.setName(n);
            opLabel.setText("name set - save + reboot device", juce::dontSendNotification);
        }
    };

    // "edit only output": the device mutes its normal stream; the DIN
    // icons here become the only MIDI source -- clean DAW MIDI-learn.
    canvas.addAndMakeVisible(editOnly);
    editOnly.onClick = [this] {
        bool on = editOnly.getToggleState();
        proc.device.setEditMode(on);
        opLabel.setColour(juce::Label::textColourId, on ? kGrey : kFg);
    };

    canvas.addAndMakeVisible(saveBtn);
    saveBtn.setColour(juce::TextButton::textColourOffId, kFg);
    canvas.addAndMakeVisible(revertBtn);
    revertBtn.setColour(juce::TextButton::textColourOffId, kGrey);
    saveBtn.onClick = [this] {
        proc.device.saveToDevice();
        opLabel.setText("saved to device", juce::dontSendNotification);
    };
    revertBtn.onClick = [this] {
        proc.device.revert();
        proc.device.requestDump();
        opLabel.setText("reverted", juce::dontSendNotification);
    };

    // ---- rows ----
    auto initRow = [&](ParamRow& r, const juce::String& label, uint8_t minV, uint8_t maxV) {
        canvas.addAndMakeVisible(r);
        r.font = eightgon;
        r.label = label;
        r.minV = minV; r.maxV = maxV;
    };
    auto initCcRow = [&](ParamRow& r, const juce::String& label) {
        initRow(r, label, 0, 127);
        r.disableable = true;
        r.hasIcon = true;
        r.onIconClick = [this, &r] { sendIconCc(r); };
    };
    auto initRelRow = [&](ParamRow& r) {
        initRow(r, "on release", 0, 127);
        r.allow255Above = true;
        r.formatter = [](uint8_t v) { return v == 255 ? "hang" : juce::String((int)v); };
    };

    initRow(presetRow, "preset", 0, 3);
    presetRow.onValue = [this](uint8_t v) { preset = v; refreshFromConfig(); };

    initRow(channelRow, "channel", 1, 16);
    channelRow.onValue = [this](uint8_t v) {
        sendParam(noteMode() ? palm::addrPreset(preset, 0) : palm::kAddrCcChannel, v);
    };

    initRow(modeRow, "mode", 0, 1);
    modeRow.toggleOnClick = true;
    modeRow.formatter = [](uint8_t v) { return v ? "note" : "cc"; };
    modeRow.onValue = [this](uint8_t v) {
        sendParam(palm::addrPreset(preset, 0), v ? (uint8_t)1 : (uint8_t)0);
        refreshFromConfig();
    };

    initCcRow(gWristCc, "wrist cc");
    gWristCc.onValue = [this](uint8_t v) { sendParam(palm::addrPreset(preset, 2), v); refreshFromConfig(); };
    initRelRow(gWristRel);
    gWristRel.onValue = [this](uint8_t v) { sendParam(palm::addrPreset(preset, 3), v); };
    initCcRow(gElbowCc, "elbow cc");
    gElbowCc.onValue = [this](uint8_t v) { sendParam(palm::addrPreset(preset, 4), v); refreshFromConfig(); };
    initRelRow(gElbowRel);
    gElbowRel.onValue = [this](uint8_t v) { sendParam(palm::addrPreset(preset, 5), v); };

    canvas.addAndMakeVisible(fingerBox);
    fingerBox.setColour(juce::ComboBox::backgroundColourId, kBg);
    fingerBox.setColour(juce::ComboBox::textColourId, kFg);
    const char* fingers[] = { "little a", "little b", "ring a", "ring b",
                              "middle a", "middle b", "index a", "index b" };
    for (int i = 0; i < 8; i++) fingerBox.addItem(fingers[i], i + 1);
    fingerBox.setSelectedItemIndex(finger, juce::dontSendNotification);
    fingerBox.onChange = [this] { finger = fingerBox.getSelectedItemIndex(); refreshFromConfig(); };

    styleLabel(touchHeader);
    touchHeader.setText("touch activated:", juce::dontSendNotification);

    initCcRow(tCc, "touch cc");
    tCc.onValue = [this](uint8_t v) { sendParam(palm::addrFinger(finger, preset, 0), v); refreshFromConfig(); };
    initRow(tRange, "range", 0, 127);
    tRange.allow255Above = true;
    tRange.formatter = [](uint8_t v) { return v == 255 ? "gate" : juce::String((int)v); };
    tRange.onValue = [this](uint8_t v) { sendParam(palm::addrFinger(finger, preset, 1), v); };
    initCcRow(fWristCc, "wrist cc");
    fWristCc.onValue = [this](uint8_t v) { sendParam(palm::addrFinger(finger, preset, 2), v); refreshFromConfig(); };
    initRelRow(fWristRel);
    fWristRel.onValue = [this](uint8_t v) { sendParam(palm::addrFinger(finger, preset, 3), v); };
    initCcRow(fElbowCc, "elbow cc");
    fElbowCc.onValue = [this](uint8_t v) { sendParam(palm::addrFinger(finger, preset, 4), v); refreshFromConfig(); };
    initRelRow(fElbowRel);
    fElbowRel.onValue = [this](uint8_t v) { sendParam(palm::addrFinger(finger, preset, 5), v); };

    initRow(selectorRow, "selector cc", 0, 127);
    selectorRow.disableable = true;
    selectorRow.onValue = [this](uint8_t v) { sendParam(palm::addrPreset(preset, 1), v); };

    initRow(chordRow, "chord", 0, 0);
    chordRow.readOnly = true;
    chordRow.formatter = [this](uint8_t) {
        int ri = rootIndexForFinger(finger);
        return ri < 0 ? juce::String("-") : chordName(kWhitePcs[ri], chordMask());
    };

    styleLabel(handLabel);
    handLabel.setJustificationType(juce::Justification::centred);
    handLabel.setInterceptsMouseClicks(false, false);  // handArea handles clicks

    layoutCanvas();

    setResizable(true, true);
    setResizeLimits(kCanvasW * 3 / 4, kCanvasH * 3 / 4, kCanvasW * 3, kCanvasH * 3);
    getConstrainer()->setFixedAspectRatio((double)kCanvasW / kCanvasH);
    setSize(kCanvasW, kCanvasH);

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

void ExoPalmEditor::resized() {
    float s = juce::jmin(getWidth() / (float)kCanvasW, getHeight() / (float)kCanvasH);
    canvas.setTransform(juce::AffineTransform::scale(s));
}

void ExoPalmEditor::layoutCanvas() {
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

    presetRects[0] = { 23, 69, 30, 30 };
    presetRects[1] = { 55, 69, 30, 30 };
    presetRects[2] = { 84, 69, 30, 30 };
}

int ExoPalmEditor::midiChannel() const {
    int ch = noteMode() ? cfg().get(palm::addrPreset(preset, 0))
                        : cfg().get(palm::kAddrCcChannel);
    return juce::jlimit(1, 16, ch);
}

void ExoPalmEditor::sendIconCc(const ParamRow& r) {
    if (r.getRaw() > 127) return;
    proc.queueMidiOut(juce::MidiMessage::controllerEvent(midiChannel(), r.getRaw(), 64));
    opLabel.setText("sent cc " + juce::String((int)r.getRaw()), juce::dontSendNotification);
}

// ---- note-mode chords ----

int ExoPalmEditor::rootFinger(int whiteIdx) const {
    return (cfg().get(palm::kAddrLeftHand) ? kRootFingersLeft : kRootFingersRight)[whiteIdx];
}

int ExoPalmEditor::rootIndexForFinger(int f) const {
    for (int i = 0; i < 7; i++)
        if (rootFinger(i) == f) return i;
    return -1;
}

uint16_t ExoPalmEditor::chordMask() const {
    int ri = rootIndexForFinger(finger);
    if (ri < 0) return 0;
    if (cfg().get(palm::addrFinger(finger, preset, 5)) == palm::kChordMaskMagic) {
        uint16_t m = (uint16_t)(cfg().get(palm::addrFinger(finger, preset, 0))
                     | (cfg().get(palm::addrFinger(finger, preset, 1)) << 7));
        if ((m & 0x0FFF) != 0) return m & 0x0FFF;
    }
    int r = kWhitePcs[ri];  // default: major triad on the pad's root
    return (uint16_t)((1u << r) | (1u << ((r + 4) % 12)) | (1u << ((r + 7) % 12)));
}

void ExoPalmEditor::toggleChordBit(int pc) {
    uint16_t m = chordMask() ^ (uint16_t)(1u << pc);
    sendParam(palm::addrFinger(finger, preset, 0), (uint8_t)(m & 0x7F));
    sendParam(palm::addrFinger(finger, preset, 1), (uint8_t)((m >> 7) & 0x1F));
    sendParam(palm::addrFinger(finger, preset, 5), palm::kChordMaskMagic);
    chordRow.repaint();
    canvas.repaint();
}

// ---- canvas drawing ----

// full-height device silhouette: cut-corner outline, preset thumb points,
// four finger pad pairs (selected = filled), button cluster, logo
void ExoPalmEditor::drawDevice(juce::Graphics& g) {
    g.setColour(kFg);
    const float x0 = 19, y0 = 59, x1 = 116, y1 = 553, c = 16;
    juce::Path body;
    body.startNewSubPath(x0 + c, y0); body.lineTo(x1 - c, y0); body.lineTo(x1, y0 + c);
    body.lineTo(x1, y1 - c); body.lineTo(x1 - c, y1); body.lineTo(x0 + c, y1);
    body.lineTo(x0, y1 - c); body.lineTo(x0, y0 + c);
    body.closeSubPath();
    g.strokePath(body, juce::PathStrokeType(1.2f));

    // preset points (thumb pads): hexagon + two ringed octagons = presets 1-3,
    // filled = the preset being edited (none filled = preset 0)
    juce::Path hex;
    hex.addPolygon({ 38.0f, 84.0f }, 6, 15.0f, juce::MathConstants<float>::halfPi);
    if (preset == 1) g.fillPath(hex);
    else g.strokePath(hex, juce::PathStrokeType(1.2f));
    for (int i = 0; i < 2; i++) {
        float cx = i == 0 ? 70.0f : 99.0f;
        juce::Path oct;
        oct.addPolygon({ cx, 84.0f }, 8, 13.0f, juce::MathConstants<float>::pi / 8.0f);
        bool active = preset == i + 2;
        if (active) g.fillPath(oct);
        else g.strokePath(oct, juce::PathStrokeType(1.2f));
        g.setColour(active ? kBg : kFg);
        g.drawEllipse(cx - 5.5f, 78.5f, 11.0f, 11.0f, 1.2f);
        g.setColour(kFg);
    }

    // finger pads: rows top->bottom index/middle/ring/little, columns a/b;
    // click selects the finger (cc mode) or the chord root pad (note mode)
    static constexpr int rowBase[4] = { 6, 4, 2, 0 };
    bool nm = noteMode();
    for (int r = 0; r < 4; r++)
        for (int col = 0; col < 2; col++) {
            int f = rowBase[r] + col;
            auto& rect = padRects[r * 2 + col];
            juce::Path oct;
            oct.addPolygon(rect.getCentre().toFloat(), 8, 17.5f,
                           juce::MathConstants<float>::pi / 8.0f);
            bool dim = nm && rootIndexForFinger(f) < 0;   // pad unused in note mode
            g.setColour(dim ? kGrey : kFg);
            if (finger == f && !dim) g.fillPath(oct);
            else g.strokePath(oct, juce::PathStrokeType(1.3f));
        }
    g.setColour(kFg);

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
    bool open = proc.device.isOpen();
    bool bt = open && proc.device.openPortIsBluetooth();

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

// note mode: one editable octave for the selected root pad's chord --
// chord tones in white, click a key to toggle it
void ExoPalmEditor::drawChordGrid(juce::Graphics& g) {
    uint16_t mask = chordMask();
    int ri = rootIndexForFinger(finger);

    for (int i = 0; i < 7; i++) {
        g.setColour((mask & (1u << kWhitePcs[i])) ? kFg : kGrey);
        g.fillRect(whiteKeyRect(i));
        if (i == ri) {  // ring marks the pad's root key
            g.setColour(kFg);
            g.drawRect(whiteKeyRect(i).expanded(3), 1);
        }
    }
    for (int j = 0; j < 5; j++) {
        g.setColour((mask & (1u << kBlackPcs[j])) ? kFg : kDark);
        g.fillRect(blackKeyRect(j));
    }
}

void ExoPalmEditor::canvasPaint(juce::Graphics& g) {
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

void ExoPalmEditor::canvasMouse(const juce::MouseEvent& e) {
    auto pos = e.getPosition();

    for (int i = 0; i < 3; i++)
        if (presetRects[i].contains(pos)) {
            preset = (preset == i + 1) ? 0 : i + 1;   // click active point = back to 0
            refreshFromConfig();
            return;
        }

    if (handArea.contains(pos)) {
        uint8_t v = cfg().get(palm::kAddrLeftHand) ? 0 : 1;
        sendParam(palm::kAddrLeftHand, v);
        refreshFromConfig();
        return;
    }

    for (int i = 0; i < 8; i++)
        if (padRects[i].contains(pos)) {
            static constexpr int rowBase[4] = { 6, 4, 2, 0 };
            int f = rowBase[i / 2] + (i % 2);
            if (noteMode() && rootIndexForFinger(f) < 0) return;  // unused pad
            finger = f;
            fingerBox.setSelectedItemIndex(finger, juce::dontSendNotification);
            refreshFromConfig();
            return;
        }

    if (noteMode() && rootIndexForFinger(finger) >= 0) {
        for (int i = 0; i < 5; i++)   // black keys sit on top
            if (blackKeyRect(i).contains(pos)) { toggleChordBit(kBlackPcs[i]); return; }
        for (int i = 0; i < 7; i++)
            if (whiteKeyRect(i).contains(pos)) { toggleChordBit(kWhitePcs[i]); return; }
    }
}

// ---- state plumbing ----

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
        tRange.setEnabled(!tCc.isOff());
        fWristRel.setEnabled(!fWristCc.isOff());
        fElbowRel.setEnabled(!fElbowCc.isOff());
    } else {
        if (rootIndexForFinger(finger) < 0) {   // snap to the c-root pad
            finger = rootFinger(0);
            fingerBox.setSelectedItemIndex(finger, juce::dontSendNotification);
        }
        selectorRow.setRaw(c.get(palm::addrPreset(preset, 1)));
        chordRow.setRaw(0);
    }

    handLabel.setText(c.get(palm::kAddrLeftHand) ? "left hand" : "right hand",
                      juce::dontSendNotification);
    loadingUi = false;
    canvas.repaint();
}

void ExoPalmEditor::sendParam(int addr, uint8_t v) {
    if (loadingUi) return;
    cfg().set(addr, v);
    proc.device.setParam(addr, v);
}

void ExoPalmEditor::openPortAt(int idx) {
    if (idx < 0 || idx >= ports.size()) return;
    if (proc.device.open(ports.getReference(idx))) {
        probing = true;   // cleared by the first reply; silent port = try next
        openedAt = juce::Time::getMillisecondCounter();
        proc.device.requestInfo();
        proc.device.requestDump();
        if (editOnly.getToggleState()) proc.device.setEditMode(true);  // reassert
        connLabel.setText(juce::String("connection: ")
                          + (ports.getReference(idx).isBluetooth ? "bt" : "hub"),
                          juce::dontSendNotification);
        canvas.repaint();
    }
}

void ExoPalmEditor::rescanPorts() {
    auto fresh = proc.device.scanPorts();
    bool same = fresh.size() == ports.size();
    if (same)
        for (int i = 0; i < fresh.size(); i++)
            if (fresh.getReference(i).name != ports.getReference(i).name) same = false;
    if (!same) {
        ports = fresh;
        portBox.clear(juce::dontSendNotification);
        int id = 1;
        for (auto& p : ports) portBox.addItem("midi port: " + p.name, id++);
        if (proc.device.isOpen())   // keep showing the connected port
            for (int i = 0; i < ports.size(); i++)
                if (ports.getReference(i).name == proc.device.openPortName())
                    portBox.setSelectedItemIndex(i, juce::dontSendNotification);
    }

    if (proc.device.isOpen()) return;

    // auto-connect: cycle through everything palm-shaped (hub ports scan
    // first) until one actually answers
    juce::Array<int> candidates;
    for (int i = 0; i < ports.size(); i++)
        if (ports.getReference(i).name.containsIgnoreCase("palm")) candidates.add(i);
    if (candidates.isEmpty()) return;
    int i = candidates[autoIdx % candidates.size()];
    portBox.setSelectedItemIndex(i, juce::dontSendNotification);
    openPortAt(i);
}

void ExoPalmEditor::timerCallback() {
    // silent port: the palm is linked over the other transport -- move on
    if (proc.device.isOpen() && probing
        && juce::Time::getMillisecondCounter() - openedAt > 4500) {
        probing = false;
        proc.device.close();
        autoIdx++;
    }
    rescanPorts();
}

void ExoPalmEditor::deviceInfoReceived(const palm::DeviceInfo& i) {
    probing = false;
    nameLabel.setText("name: " + juce::String(i.name), juce::dontSendNotification);
    opLabel.setText("normal operation", juce::dontSendNotification);
}

void ExoPalmEditor::configReceived(const palm::Config& c) {
    probing = false;
    proc.lastConfig = c;
    refreshFromConfig();
}

void ExoPalmEditor::connectionChanged() {
    if (!proc.device.isOpen()) {
        connLabel.setText("connection: -", juce::dontSendNotification);
        opLabel.setText("not connected", juce::dontSendNotification);
    }
    canvas.repaint();
}

void ExoPalmEditor::midiActivity(const juce::MidiMessage& m) {
    probing = false;   // any traffic proves the port is alive
    MonRow r;
    if (m.isController()) { r.msg = "cc"; r.num = juce::String(m.getControllerNumber()); r.val = juce::String(m.getControllerValue()); }
    else if (m.isNoteOn()) { r.msg = "note on"; r.num = juce::MidiMessage::getMidiNoteName(m.getNoteNumber(), true, true, 3); r.val = juce::String((int)m.getVelocity()); }
    else if (m.isNoteOff()) { r.msg = "note off"; r.num = juce::MidiMessage::getMidiNoteName(m.getNoteNumber(), true, true, 3); r.val = "0"; }
    else return;
    r.ch = juce::String(m.getChannel());
    monitor.push_front(r);
    while (monitor.size() > 6) monitor.pop_back();
    canvas.repaint(kColMsg - 4, 380, kCanvasW - kColMsg, 160);
}
