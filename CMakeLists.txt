cmake_minimum_required(VERSION 3.22)
project(ELXAR_Two VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  # Universal (Apple silicon + Intel) is set by the CI workflow; macOS 11+ runs on both of Bryn's Macs.
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

# ---------------------------------------------------------------------------------------------------------
# The sound engine: the LXR-02 firmware's own DSP code (third_party/lxr-firmware, GPL-3.0) plus a small glue
# file. No JUCE, no hardware. Built as a plain C library so the headless engine_test can use it too.
# ---------------------------------------------------------------------------------------------------------
set(LXR third_party/lxr-firmware/Core)
file(GLOB LXR_DSP_SOURCES CONFIGURE_DEPENDS
  ${LXR}/Src/Soundengine/DSPAudio/*.c
  ${LXR}/Src/Soundengine/DSPAudio/FX/*.c
  ${LXR}/Src/Soundengine/DSPAudio/FX/VerbTools/*.c)
# random.c talks to the STM32 random-number chip; src/engine/elxar_engine.c provides a software replacement.
list(FILTER LXR_DSP_SOURCES EXCLUDE REGEX "/DSPAudio/random\\.c$")

add_library(elxar_core STATIC
  ${LXR_DSP_SOURCES}
  ${LXR}/Src/Soundengine/FxBus.c
  ${LXR}/Src/Soundengine/MIDI/ParameterArray.c
  ${LXR}/Src/Soundengine/MIDI/MidiParser.c
  ${LXR}/Src/Soundengine/MIDI/MidiVoiceControl.c
  ${LXR}/Src/Soundengine/Sequencer/sequencer.c
  ${LXR}/Src/Soundengine/Sequencer/EuklidGenerator.c
  ${LXR}/Src/Parameters.c
  ${LXR}/Src/menu/Cc2Text.c
  ${LXR}/Src/Preset/initKit.c
  src/engine/elxar_engine.c)
# src/engine/shim comes first: it stands in for the STM32 hardware headers.
target_include_directories(elxar_core PUBLIC
  src/engine/shim
  src/engine
  ${LXR}/Inc
  ${LXR}/Inc/menu
  ${LXR}/Inc/Soundengine/MIDI)
# ELXAR_TWO_HOST: plugin build (no special STM32 memory sections). USE_REVERB=1: switch on the firmware's plate reverb.
target_compile_definitions(elxar_core PUBLIC ELXAR_TWO_HOST=1 USE_REVERB=1)
set_target_properties(elxar_core PROPERTIES
  C_STANDARD 11
  C_EXTENSIONS ON
  POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  # -fno-strict-aliasing / -fgnu89-inline: the firmware was written for one specific ARM compiler setup and relies on both.
  # -w: do not print the firmware's many harmless warnings.
  # -g: keep function and line names so a crash in the engine test can be traced (does not change the plugin's sound or size on macOS).
  target_compile_options(elxar_core PRIVATE -fno-strict-aliasing -fgnu89-inline -w -g)
  # The delay and reverb store half-precision numbers (__fp16). On Intel, -mf16c makes the processor convert them
  # directly instead of calling system helper functions whose behaviour differs between macOS versions.
  if(APPLE)
    target_compile_options(elxar_core PRIVATE "SHELL:-Xarch_x86_64 -mf16c")
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    target_compile_options(elxar_core PRIVATE -mf16c)
  endif()
endif()

# Headless test: renders drum hits to WAV files and checks them. Needs no JUCE, so it can be built on its own:
#   cmake -S . -B build -DELXAR_BUILD_PLUGIN=OFF && cmake --build build --target engine_test
add_executable(engine_test tools/engine_test/engine_test.cpp)
target_link_libraries(engine_test PRIVATE elxar_core)
target_compile_options(engine_test PRIVATE -g)
if(UNIX AND NOT APPLE)
  target_link_libraries(engine_test PRIVATE m)
endif()

option(ELXAR_BUILD_PLUGIN "Build the AU / VST3 plugin (downloads JUCE)" ON)
if(ELXAR_BUILD_PLUGIN)
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
  # For trying the window without a host (not part of the download): -DELXAR_EXTRA_FORMATS=Standalone
  set(ELXAR_EXTRA_FORMATS "" CACHE STRING "Extra plugin formats for local testing")
  list(APPEND FORMATS ${ELXAR_EXTRA_FORMATS})

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

  target_include_directories(ELXAR_Two PRIVATE src)

  target_compile_definitions(ELXAR_Two PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    ELXAR_VERSION_STRING="${PROJECT_VERSION}")

  target_link_libraries(ELXAR_Two PRIVATE
    elxar_core
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

  # Plugin test (not part of the download): runs the plugin code without a host. Off unless asked for:
  #   cmake -S . -B build -DELXAR_BUILD_PLUGIN_TEST=ON && cmake --build build --target ELXAR_Two_PluginTest
  option(ELXAR_BUILD_PLUGIN_TEST "Build the plugin test program" OFF)
  if(ELXAR_BUILD_PLUGIN_TEST)
    juce_add_console_app(ELXAR_Two_PluginTest)
    target_sources(ELXAR_Two_PluginTest PRIVATE
      tools/plugin_test/plugin_test.cpp
      src/PluginProcessor.cpp
      src/PluginEditor.cpp)
    target_include_directories(ELXAR_Two_PluginTest PRIVATE src)
    target_compile_definitions(ELXAR_Two_PluginTest PRIVATE
      JUCE_WEB_BROWSER=0
      JUCE_USE_CURL=0
      ELXAR_VERSION_STRING="${PROJECT_VERSION}")
    target_link_libraries(ELXAR_Two_PluginTest PRIVATE
      elxar_core
      juce::juce_audio_utils
      juce::juce_recommended_config_flags)
  endif()
endif()
