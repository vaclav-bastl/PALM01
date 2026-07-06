# exoPALM configurator — design

A VST3/AU/CLAP plugin + standalone app that configures the PALM live over
MIDI. Settings live in the PALM's flash (NVS), not in firmware code. The UI
follows `UImockup.png` (Eightgon font, black/white, one screen whose layout
varies by preset mode: CC vs NOTE; deactivated parameters render grey).

## Architecture

    plugin (JUCE, direct CoreMIDI) <--SysEx--> exohub port ---ESP-NOW--> PALM
                                   <--SysEx--> PALM_03 Bluetooth (direct BLE)

The plugin opens the MIDI port itself (no DAW routing): port picker in the
header ("midi port:"), preferring "exohub PALM_03", falling back to
"PALM_03 Bluetooth" ("connection: auto / bt" in the mockup).

## SysEx protocol

Frame: `F0 7D 'P' 'L' 'M' <cmd> <payload...> F7` (0x7D = research/dev
manufacturer ID — swap for a real one before release). All payload bytes
7-bit; config bytes >127 are sent as two 7-bit halves (lo, hi).

| cmd | direction | meaning |
|-----|-----------|---------|
| 0x01 | host→PALM | request device info |
| 0x41 | PALM→host | device info: schema version, fw version, name (ASCII) |
| 0x02 | host→PALM | request full config dump |
| 0x42 | PALM→host | full config (encoded blob, schema-versioned) |
| 0x03 | host→PALM | set parameter: addr(2×7bit) value(2×7bit) — applies live |
| 0x04 | host→PALM | save current config to NVS |
| 0x05 | host→PALM | set device name (ASCII ≤16; applies after reboot) |
| 0x06 | host→PALM | revert: reload NVS (disc discards live edits) |
| 0x07 | host→PALM | edit mode: payload 1=mute normal MIDI output (plugin DIN icons send instead, for DAW MIDI-learn), 0=restore. RAM only; reboot clears |

Parameter address space (byte offsets into the config blob):
- 0: leftHand (0/1)
- 1: ccModeChannel (1..16)
- per preset p in 0..3 at 2+p*6: mode(0=cc, 1..16=note-ch), selectorCC,
  G_WRIST_CC, G_WRIST_RESET, G_ELBOW_CC, G_ELBOW_RESET
- per finger f in 0..7, preset p at 26+ (f*4+p)*6: TOUCH_CC, TOUCH_RANGE,
  WRIST_CC, WRIST_RESET, ELBOW_CC, ELBOW_RESET
- total: 26 + 192 = 218 bytes (+ name stored separately)

NOTE presets repurpose the (otherwise unused) finger bytes per chord root:
byte 0 = 12-bit pitch-class mask lo7, byte 1 = mask hi5, byte 5 = 0x42
(magic marking the mask valid). The root finger's mask replaces the fixed
triad/dom7 arpeggio tables, built relative to the root across 4 octaves.
Roots map fingers→white keys c..b exactly as `getRootFromTouchesLocal`.

## Firmware

- `config.ino`: PalmConfig load/save (Preferences/NVS, versioned blob);
  existing global tables stay as the runtime representation — seeded from
  NVS at boot, mutated live by set-param, persisted by save. Current code
  values become first-boot defaults.
- `sysex.ino`: frame parser (accumulates F0..F7 across BLE packets and
  ESP-NOW chunks) + command handlers + reply sender (routes out through
  whichever transport the request came from).
- BLE RX: MIDI characteristic write callback added (PALM previously had
  no BLE input parsing).
- Device name in NVS drives BLE_DEVICE_NAME + the ESP-NOW PING announce.

## exohub

- Upstream: SysEx bytes from a sender stream to that sender's USB port
  (usbMidi.write() handles USB-MIDI SysEx packetization).
- Downstream: USB SysEx packets (CIN 4-7) on port N are chunked over
  ESP-NOW to sender N, ordered+retried by the existing seq/ACK layer.

## Plugin

JUCE (CMake) + clap-juce-extensions. Targets: VST3, AU, CLAP, Standalone.
- Model: ParamBlob mirroring the address space; dirty tracking; live edits
  send 0x03 immediately (audible), Save button sends 0x04.
- MIDI monitor pane: decodes traffic on the selected port (mockup bottom).
- "edit only output": toggle to suspend live sending while editing.
- DAW recall (later): plugin state stores the blob; on load offer "push to
  device".

## Phases

1. firmware config → NVS (behavior unchanged, verified by playing)
2. SysEx over BLE, verified with a CoreMIDI script before any plugin code
3. SysEx over exohub (chunking), same script via the exohub port
4. JUCE scaffold + protocol client (headless round-trip test)
5. UI per mockup
