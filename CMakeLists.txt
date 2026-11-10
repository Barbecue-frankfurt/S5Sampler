# Must be set before project() so the macOS build becomes a universal binary
# (Apple Silicon + Intel). Cubase on Apple Silicon needs the arm64 slice,
# Cubase running under Rosetta needs x86_64.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for macOS")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

cmake_minimum_required(VERSION 3.22)
project(S5Sampler VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.16-security
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

set(S5_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND S5_FORMATS AU)                  # Audio Unit for Logic / GarageBand / MainStage
endif()

juce_add_plugin(S5Sampler
    COMPANY_NAME                "S5Project"
    PRODUCT_NAME                "S5 Sampler"
    PLUGIN_MANUFACTURER_CODE    S5Pj
    PLUGIN_CODE                 S5sm
    FORMATS                     ${S5_FORMATS}
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    VST3_CATEGORIES             Instrument Sampler
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    # Copies the finished VST3 to ~/Library/Audio/Plug-Ins/VST3 on macOS
    COPY_PLUGIN_AFTER_BUILD     TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "S5 Sampler uses the audio input for the Wave Scope and for sampling."
)

target_sources(S5Sampler PRIVATE
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/Screen/PixelScreen.cpp
    Source/Screen/Pages.cpp
    Source/Screen/PageImages.cpp
    Source/Screen/UiState.cpp
    Source/Screen/LivePage.cpp
    Source/Screen/LivePages.cpp
    Source/Screen/App.cpp
    Source/Midi/SysEx.cpp
    Source/Data/Block.cpp
    Source/Data/WaveImport.cpp
    Source/Data/Hfe.cpp
    Source/Data/HdImage.cpp
    Source/Data/BlockOps.cpp
    Source/Engine/Engine.cpp
    Source/Platform/MacFloppy.cpp
    Source/Platform/HdOptions.cpp
    Source/Platform/MidiLink.cpp
    Source/Platform/MidiLinkUi.cpp
    Source/Screen/ScreenComponent.cpp
    Source/Screen/ScreenEditor.cpp
)

target_compile_definitions(S5Sampler PUBLIC
    S5_EMPTY_START=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(S5Sampler
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
