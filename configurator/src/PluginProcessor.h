// exoPALM configurator -- audio-passthrough plugin shell. All real work
// happens in the editor + PalmDevice (direct MIDI); the processor exists
// so the configurator can sit on any track without touching the audio.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PalmDevice.h"

class ExoPalmProcessor : public juce::AudioProcessor {
public:
    ExoPalmProcessor()
        : juce::AudioProcessor(BusesProperties()
                                   .withInput("Input", juce::AudioChannelSet::stereo(), true)
                                   .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}  // passthrough

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "exoPALM"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // DAW project recall: last known device config travels with the session
    void getStateInformation(juce::MemoryBlock& dest) override {
        dest.replaceAll(lastConfig.blob, palm::kBlobSize);
    }
    void setStateInformation(const void* data, int size) override {
        if (size == palm::kBlobSize) memcpy(lastConfig.blob, data, (size_t)size);
    }

    PalmDevice device;          // shared with the editor
    palm::Config lastConfig;    // editor keeps this in sync

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExoPalmProcessor)
};
