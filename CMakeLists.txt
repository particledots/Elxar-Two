cmake_minimum_required(VERSION 3.22)
project(ELXAR_Two VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  # Universal (Apple silicon + Intel) is set by the CI workflow; macOS 11+ runs on both of Bryn's Macs.
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Plugin formats: AU (macOS only, the priority) + VST3.
set(FORMATS VST3)
if(APPLE)
  list(APPEND FORMATS AU)
endif()

# AU instrument: JUCE sets the Audio Unit type to kAudioUnitType_MusicDevice ("aumu") when IS_SYNTH is TRUE.
juce_add_plugin(ELXAR_Two
  COMPANY_NAME "particledots"
  PLUGIN_MANUFACTURER_CODE Ptdt
  PLUGIN_CODE Elx2
  FORMATS ${FORMATS}
  PRODUCT_NAME "ELXAR-Two"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(ELXAR_Two PRIVATE
  src/PluginProcessor.cpp
  src/PluginEditor.cpp)

target_compile_definitions(ELXAR_Two PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  ELXAR_VERSION_STRING="${PROJECT_VERSION}")

target_link_libraries(ELXAR_Two PRIVATE
  juce::juce_audio_utils
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)

# Milestone 0: the Erica LXR-02 source in third_party/lxr-firmware is imported but NOT yet compiled in.
# Milestone 1 decouples the engine from the STM32 hardware and links it here.
