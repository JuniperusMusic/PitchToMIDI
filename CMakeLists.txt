cmake_minimum_required(VERSION 3.22)

project(ThereBend VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static MSVC runtime so the .vst3 has no Visual C++ redistributable dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(THEREBEND_BUILD_TESTS "Build the engine test executable" ON)

# Use a local JUCE checkout with -DJUCE_DIR=/path/to/JUCE, otherwise download it.
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.10
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(ThereBend
    COMPANY_NAME "ThereBend"
    BUNDLE_ID "com.therebend.therebend"
    PLUGIN_MANUFACTURER_CODE Thbn
    PLUGIN_CODE Tb2m
    PRODUCT_NAME "ThereBend"
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Analyzer)

set(ENGINE_SOURCES
    Source/PitchDetector.cpp
    Source/MidiPitchEncoder.cpp
    Source/ThereminEngine.cpp)

target_sources(ThereBend PRIVATE
    ${ENGINE_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(ThereBend PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ThereBend
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(THEREBEND_BUILD_TESTS)
    juce_add_console_app(ThereBendTests PRODUCT_NAME "ThereBendTests")
    target_sources(ThereBendTests PRIVATE Tests/EngineTests.cpp ${ENGINE_SOURCES})
    target_include_directories(ThereBendTests PRIVATE Source)
    target_compile_definitions(ThereBendTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(ThereBendTests
        PRIVATE juce::juce_dsp juce::juce_audio_basics
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
