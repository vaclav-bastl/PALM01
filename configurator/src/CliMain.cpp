// palmcli -- console client for the exoPALM SysEx protocol. Exercises the
// same PalmProtocol logic as the plugin; used for automated verification.
//
//   palmcli ports            list candidate ports
//   palmcli info|dump        query the device (prefers exohub port)
//   palmcli set <addr> <val> live-set one parameter
//   palmcli save | revert
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>
#include "PalmProtocol.h"
#include <atomic>
#include <cstdio>

struct CliSink : juce::MidiInputCallback {
    std::atomic<bool> gotReply { false };
    std::vector<uint8_t> reply;
    juce::CriticalSection lock;

    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override {
        if (!m.isSysEx()) return;
        const uint8_t* d = m.getRawData();
        int len = m.getRawDataSize();
        if (!palm::isPalmFrame(d, len)) return;
        const juce::ScopedLock sl(lock);
        reply.assign(d, d + len);
        gotReply = true;
    }
};

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::String cmd = argc > 1 ? argv[1] : "info";

    auto ins = juce::MidiInput::getAvailableDevices();
    auto outs = juce::MidiOutput::getAvailableDevices();

    if (cmd == "ports") {
        for (auto& i : ins)
            if (i.name.startsWith("exohub") || i.name.containsIgnoreCase("Bluetooth"))
                printf("%s\n", i.name.toRawUTF8());
        return 0;
    }

    // pick a port: exohub PALM first, then PALM_03 Bluetooth
    juce::MidiDeviceInfo inDev, outDev;
    auto pick = [&](const juce::String& needle) {
        for (auto& i : ins)
            if (i.name.contains(needle))
                for (auto& o : outs)
                    if (o.name == i.name) { inDev = i; outDev = o; return true; }
        return false;
    };
    if (!pick("exohub PALM") && !pick("PALM_03 Bluetooth")) {
        fprintf(stderr, "no PALM port found\n");
        return 1;
    }
    fprintf(stderr, "using port: %s\n", inDev.name.toRawUTF8());

    CliSink sink;
    auto in = juce::MidiInput::openDevice(inDev.identifier, &sink);
    auto out = juce::MidiOutput::openDevice(outDev.identifier);
    if (in == nullptr || out == nullptr) { fprintf(stderr, "open failed\n"); return 1; }
    in->start();

    auto send = [&](const std::vector<uint8_t>& f) {
        out->sendMessageNow(juce::MidiMessage(f.data(), (int)f.size()));
    };
    auto waitReply = [&](int ms) {
        for (int t = 0; t < ms; t += 20) {
            if (sink.gotReply) return true;
            juce::Thread::sleep(20);
        }
        return false;
    };

    if (cmd == "info") {
        send(palm::buildInfoRequest());
        if (!waitReply(4000)) { fprintf(stderr, "timeout\n"); return 1; }
        const juce::ScopedLock sl(sink.lock);
        if (auto i = palm::parseInfo(sink.reply.data(), (int)sink.reply.size())) {
            printf("schema=%d fw=%d name=%s\n", i->schemaVersion, i->fwVersion, i->name.c_str());
            return 0;
        }
        fprintf(stderr, "bad reply\n");
        return 1;
    }
    if (cmd == "dump") {
        send(palm::buildDumpRequest());
        if (!waitReply(6000)) { fprintf(stderr, "timeout\n"); return 1; }
        const juce::ScopedLock sl(sink.lock);
        if (auto c = palm::parseDump(sink.reply.data(), (int)sink.reply.size())) {
            printf("leftHand=%d ccChannel=%d\n", c->get(palm::kAddrLeftHand), c->get(palm::kAddrCcChannel));
            for (int p = 0; p < palm::kNumPresets; p++)
                printf("preset %d: mode=%d selCC=%d gWrist=%d/%d gElbow=%d/%d\n", p,
                       c->get(palm::addrPreset(p, 0)), c->get(palm::addrPreset(p, 1)),
                       c->get(palm::addrPreset(p, 2)), c->get(palm::addrPreset(p, 3)),
                       c->get(palm::addrPreset(p, 4)), c->get(palm::addrPreset(p, 5)));
            for (int f = 0; f < palm::kNumFingers; f++) {
                printf("finger %d:", f);
                for (int p = 0; p < palm::kNumPresets; p++)
                    printf("  [%d %d %d %d %d %d]",
                           c->get(palm::addrFinger(f, p, 0)), c->get(palm::addrFinger(f, p, 1)),
                           c->get(palm::addrFinger(f, p, 2)), c->get(palm::addrFinger(f, p, 3)),
                           c->get(palm::addrFinger(f, p, 4)), c->get(palm::addrFinger(f, p, 5)));
                printf("\n");
            }
            return 0;
        }
        fprintf(stderr, "bad reply\n");
        return 1;
    }
    if (cmd == "set" && argc >= 4) {
        send(palm::buildSetParam(atoi(argv[2]), (uint8_t)atoi(argv[3])));
        juce::Thread::sleep(200);
        printf("sent\n");
        return 0;
    }
    if (cmd == "save") { send(palm::buildSave()); juce::Thread::sleep(200); printf("sent\n"); return 0; }
    if (cmd == "revert") { send(palm::buildRevert()); juce::Thread::sleep(200); printf("sent\n"); return 0; }

    fprintf(stderr, "usage: palmcli ports|info|dump|set <addr> <val>|save|revert\n");
    return 1;
}
