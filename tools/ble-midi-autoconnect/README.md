# ble-midi-autoconnect

Keeps bonded BLE MIDI instruments (PALM_03, EVA_MIC_01, ...) auto-reconnecting
on macOS: a tiny daemon pokes the CoreMIDI Bluetooth driver every 10 s, which
reconnects any bonded device that is currently advertising. Device-agnostic —
one install covers every instrument. macOS never initiates BLE MIDI
reconnection by itself; this is the same mechanism the "BLE MIDI Connect"
style utilities use.

Build:

    swiftc -O -o ble-midi-autoconnect main.swift

Install (runs at login, restarts if it exits):

    cp com.exo.ble-midi-autoconnect.plist ~/Library/LaunchAgents/
    launchctl load ~/Library/LaunchAgents/com.exo.ble-midi-autoconnect.plist

Remove:

    launchctl unload ~/Library/LaunchAgents/com.exo.ble-midi-autoconnect.plist
    rm ~/Library/LaunchAgents/com.exo.ble-midi-autoconnect.plist

One-shot test without installing:

    ./ble-midi-autoconnect 10   # Ctrl+C to stop

Uses a private CoreMIDI symbol; if a macOS update ever removes it, the daemon
exits with a clear message instead of misbehaving
(check with: launchctl list | grep exo).
