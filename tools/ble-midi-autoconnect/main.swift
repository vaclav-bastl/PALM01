// ble-midi-autoconnect — keep bonded BLE MIDI devices (PALM_03, EVA_MIC_01, ...)
// auto-reconnecting on macOS.
//
// macOS never initiates BLE MIDI reconnection by itself; this daemon
// periodically calls the CoreMIDI Bluetooth driver's activate-all call
// (same mechanism the "BLE MIDI Connect" utilities use). Idempotent:
// already-connected devices are untouched; absent devices are ignored.
// NOTE: uses a private CoreMIDI symbol -- if a macOS update removes it,
// the daemon exits with a clear message instead of misbehaving.

import Foundation

typealias ActivateFn = @convention(c) () -> Void

guard let handle = dlopen("/System/Library/Frameworks/CoreMIDI.framework/CoreMIDI", RTLD_NOW),
      let sym = dlsym(handle, "MIDIBluetoothDriverActivateAllConnections") else {
  fputs("MIDIBluetoothDriverActivateAllConnections not found (macOS changed?)\n", stderr)
  exit(1)
}
let activate = unsafeBitCast(sym, to: ActivateFn.self)

let interval = CommandLine.arguments.count > 1 ? (Double(CommandLine.arguments[1]) ?? 10) : 10

while true {
  activate()
  RunLoop.current.run(until: Date(timeIntervalSinceNow: interval))
}
