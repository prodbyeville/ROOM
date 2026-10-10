# ROOM plugin (working name). Builds a VST3 plus a command-line renderer
# used to test the DSP without a DAW.
#
#   cmake -S plugin -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
#
# JUCE is downloaded automatically. To use a local copy instead, add
# -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE

cmake_minimum_required(VERSION 3.22)
project(ROOM VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if (MSVC)
    # Static runtime so the plugin doesn't depend on the VC++ redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# DSP sources shared by the plugin and the test renderer. They're compiled
# into each target directly (JUCE modules must only be linked once per binary).
set(ROOM_DSP_SOURCES
    Source/dsp/RoomAnalysis.cpp
    Source/dsp/RoomSynth.cpp
    Source/dsp/RoomEngine.cpp)

set(ROOM_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1)

# ---------------------------------------------------------------------------
# The plugin
# ---------------------------------------------------------------------------
juce_add_plugin(ROOM
    COMPANY_NAME "Eville"
    BUNDLE_ID "com.prodbyeville.room"
    PLUGIN_MANUFACTURER_CODE Evll
    PLUGIN_CODE Room
    FORMATS VST3
    PRODUCT_NAME "ROOM"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Reverb)

target_sources(ROOM PRIVATE
    ${ROOM_DSP_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_include_directories(ROOM PRIVATE Source)
target_compile_definitions(ROOM PUBLIC ${ROOM_DEFINITIONS})
target_link_libraries(ROOM
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Command-line renderer: room_render SOURCE TARGET OUT [intensity] [mix]
# Runs the exact plugin DSP offline, in host-sized blocks.
# ---------------------------------------------------------------------------
juce_add_console_app(room_render PRODUCT_NAME "room_render")
target_sources(room_render PRIVATE ${ROOM_DSP_SOURCES} Tools/RoomRender.cpp)
target_include_directories(room_render PRIVATE Source)
target_compile_definitions(room_render PRIVATE ${ROOM_DEFINITIONS})
target_link_libraries(room_render
    PRIVATE
        juce::juce_audio_formats
        juce::juce_data_structures
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Host test: loads the built VST3 like a DAW and checks room loading, state
# save/restore and parameter changes while playing (see Tools/HostTest.cpp).
# ---------------------------------------------------------------------------
option(ROOM_BUILD_TESTS "Build the room_hosttest program" ON)
if (ROOM_BUILD_TESTS)
    juce_add_console_app(room_hosttest PRODUCT_NAME "room_hosttest")
    target_sources(room_hosttest PRIVATE Source/dsp/RoomAnalysis.cpp Tools/HostTest.cpp)
    target_include_directories(room_hosttest PRIVATE Source)
    target_compile_definitions(room_hosttest PRIVATE
        ${ROOM_DEFINITIONS}
        JUCE_PLUGINHOST_VST3=1
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(room_hosttest
        PRIVATE
            juce::juce_audio_processors
            juce::juce_audio_formats
            juce::juce_gui_basics
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    add_dependencies(room_hosttest ROOM_VST3)
endif()
