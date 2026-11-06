cmake_minimum_required(VERSION 3.22)
project(MuziTame VERSION 0.6.3)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "")
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: point at a local checkout with -DJUCE_DIR=/path/to/JUCE, otherwise it is downloaded.
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 9.0.5 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(APPLE)
    set(MT_FORMATS AU VST3 Standalone)
else()
    set(MT_FORMATS VST3 Standalone)
endif()
if(DEFINED MT_FORMATS_OVERRIDE)
    set(MT_FORMATS ${MT_FORMATS_OVERRIDE})
endif()

juce_add_plugin(MuziTame
    COMPANY_NAME "Muzi"
    PRODUCT_NAME "Muzi Tame"
    VERSION 0.6.3
    PLUGIN_MANUFACTURER_CODE Muzi
    PLUGIN_CODE MzTm
    FORMATS ${MT_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx EQ
    AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(MuziTame PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(MuziTame PRIVATE Source)

target_compile_definitions(MuziTame PUBLIC
    MUZI_USE_JUCE_FFT=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(MuziTame
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Headless VST3 host used by the tests (not part of the plugin):
#   cmake -B build -DMT_BUILD_TEST_HOST=ON && cmake --build build --target MuziHostTest
option(MT_BUILD_TEST_HOST "Build the headless VST3 test host" OFF)
if(MT_BUILD_TEST_HOST)
    juce_add_console_app(MuziHostTest PRODUCT_NAME "MuziHostTest")
    target_sources(MuziHostTest PRIVATE tests/host/HostTest.cpp)
    target_compile_definitions(MuziHostTest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(MuziHostTest PRIVATE juce::juce_audio_processors juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)
endif()

# GUI snapshot tool for headless checks (not part of the plugin)
if(MT_BUILD_TEST_HOST)
    juce_add_console_app(MuziSnapshot PRODUCT_NAME "MuziSnapshot")
    target_sources(MuziSnapshot PRIVATE tests/snapshot/Snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(MuziSnapshot PRIVATE Source)
    target_compile_definitions(MuziSnapshot PRIVATE MUZI_USE_JUCE_FFT=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JucePlugin_Name="Muzi Tame" JucePlugin_Build_Standalone=0)
    target_link_libraries(MuziSnapshot PRIVATE juce::juce_dsp)
    target_link_libraries(MuziSnapshot PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics
        PUBLIC juce::juce_recommended_config_flags)
endif()

# Editor CPU benchmark (not part of the plugin)
if(MT_BUILD_TEST_HOST)
    juce_add_console_app(MuziUiBench PRODUCT_NAME "MuziUiBench")
    target_sources(MuziUiBench PRIVATE tests/snapshot/UiBench.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(MuziUiBench PRIVATE Source)
    target_compile_definitions(MuziUiBench PRIVATE MUZI_USE_JUCE_FFT=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JucePlugin_Name="Muzi Tame" JucePlugin_Build_Standalone=0)
    target_link_libraries(MuziUiBench PRIVATE juce::juce_dsp juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics
        PUBLIC juce::juce_recommended_config_flags)
endif()

# Performance report (audio blocks + display), run on real Macs by .github/workflows/perf.yml
option(MT_BUILD_PERF "Build the MuziPerf report tool" OFF)
if(MT_BUILD_PERF)
    juce_add_console_app(MuziPerf PRODUCT_NAME "MuziPerf")
    target_sources(MuziPerf PRIVATE tests/perf/Perf.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(MuziPerf PRIVATE Source)
    target_compile_definitions(MuziPerf PRIVATE MUZI_USE_JUCE_FFT=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JucePlugin_Name="Muzi Tame" JucePlugin_Build_Standalone=0)
    target_link_libraries(MuziPerf PRIVATE juce::juce_dsp juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
endif()
