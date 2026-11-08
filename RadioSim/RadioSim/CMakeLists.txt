cmake_minimum_required(VERSION 3.22)
project(RadioSim VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JUCE_TAG "8.0.4" CACHE STRING "JUCE git tag to build against")
include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG ${JUCE_TAG} GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(RadioSim
    COMPANY_NAME "RadioSim"
    PLUGIN_MANUFACTURER_CODE Rdsm
    PLUGIN_CODE Rsm1
    FORMATS VST3
    PRODUCT_NAME "RadioSim"
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(RadioSim PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(RadioSim PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(RadioSim PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
