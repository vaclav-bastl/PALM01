#include "PluginProcessor.h"

// JUCE plugin entry point
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new ExoPalmProcessor();
}
