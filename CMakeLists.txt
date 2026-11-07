cmake_minimum_required(VERSION 3.22)

project(SenseiSmooth VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Link the MSVC runtime statically, so the plugin runs without the
# Visual C++ Redistributable being installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE: prefer a local checkout in ./JUCE, otherwise fetch it.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.6
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------ plugin
juce_add_plugin(SenseiSmooth
    COMPANY_NAME                "vierminus audio"
    BUNDLE_ID                   com.vierminus.senseismooth
    PLUGIN_MANUFACTURER_CODE    Vmau
    PLUGIN_CODE                 Snsm
    PRODUCT_NAME                "Sensei Smooth"
    FORMATS                     VST3 Standalone
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Dynamics EQ
    ICON_BIG                    "${CMAKE_CURRENT_SOURCE_DIR}/assets/graphics/ss_appicon_1024.png"
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(SenseiSmooth PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ResonanceSuppressor.cpp)

# Logo for the editor header
juce_add_binary_data(SenseiSmoothAssets
    HEADER_NAME SenseiSmoothAssets.h
    NAMESPACE   SenseiSmoothAssets
    SOURCES     assets/graphics/ss_logo_white.svg)
target_link_libraries(SenseiSmooth PRIVATE SenseiSmoothAssets)

target_compile_definitions(SenseiSmooth PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(SenseiSmooth
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------ tests
option(SENSEISMOOTH_BUILD_TESTS "Build the offline DSP tests" ON)

if(SENSEISMOOTH_BUILD_TESTS)
    juce_add_console_app(SenseiSmoothTests PRODUCT_NAME "SenseiSmoothTests")

    target_sources(SenseiSmoothTests PRIVATE
        Tests/DspTests.cpp
        Source/ResonanceSuppressor.cpp)

    target_include_directories(SenseiSmoothTests PRIVATE Source)

    target_compile_definitions(SenseiSmoothTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

    target_link_libraries(SenseiSmoothTests
        PRIVATE
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # Renders the editor offscreen to a PNG (visual check without a host).
    juce_add_console_app(SenseiSmoothSnapshot PRODUCT_NAME "SenseiSmoothSnapshot")
    target_sources(SenseiSmoothSnapshot PRIVATE Tests/Snapshot.cpp)
    # Reuse the plugin's compiled shared code; take over its JUCE include paths
    # and module definitions without compiling the JUCE modules a second time.
    target_include_directories(SenseiSmoothSnapshot PRIVATE Source $<TARGET_PROPERTY:SenseiSmooth,INCLUDE_DIRECTORIES>)
    target_compile_definitions(SenseiSmoothSnapshot PRIVATE $<TARGET_PROPERTY:SenseiSmooth,COMPILE_DEFINITIONS>)
    target_link_libraries(SenseiSmoothSnapshot PRIVATE SenseiSmooth)
endif()
