cmake_minimum_required(VERSION 3.22)
project(exoPALMConfigurator VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

include(FetchContent)

FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.8
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

FetchContent_Declare(clap-juce-extensions
  GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
  GIT_TAG main
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(clap-juce-extensions)

# ---- the plugin (VST3 / AU / CLAP / Standalone) ----
juce_add_plugin(exoPALM
  COMPANY_NAME "Bastl Instruments"
  BUNDLE_ID com.bastl.exopalm
  PLUGIN_MANUFACTURER_CODE Bstl
  PLUGIN_CODE Xpal
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  COPY_PLUGIN_AFTER_BUILD TRUE
  FORMATS VST3 AU Standalone
  PRODUCT_NAME "exoPALM")

clap_juce_extensions_plugin(TARGET exoPALM
  CLAP_ID "com.bastl.exopalm"
  CLAP_FEATURES utility)

juce_add_binary_data(exoPALMAssets
  SOURCES ../vst-design/eightgon-font/Eightgon-OGn6p.ttf)

target_sources(exoPALM PRIVATE
  src/PluginProcessor.cpp
  src/PluginEditor.cpp
  src/PalmProtocol.cpp
  src/PalmDevice.cpp)

target_compile_definitions(exoPALM PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=1)

target_link_libraries(exoPALM
  PRIVATE exoPALMAssets juce::juce_audio_utils juce::juce_audio_devices
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# ---- console client (automated protocol verification) ----
juce_add_console_app(palmcli PRODUCT_NAME "palmcli")
target_sources(palmcli PRIVATE src/CliMain.cpp src/PalmProtocol.cpp)
target_compile_definitions(palmcli PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(palmcli PRIVATE
  juce::juce_audio_devices juce::juce_events
  juce::juce_recommended_config_flags)
