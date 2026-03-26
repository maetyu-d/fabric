cmake_minimum_required(VERSION 3.20)

project(Fabric VERSION 0.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PULSE_JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "Path to a local JUCE checkout")

if (NOT EXISTS "${PULSE_JUCE_PATH}/CMakeLists.txt" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../Orbits/JUCE/CMakeLists.txt")
  set(PULSE_JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../Orbits/JUCE" CACHE PATH "Path to a local JUCE checkout" FORCE)
endif()

find_package(JUCE CONFIG QUIET)

if (NOT JUCE_FOUND)
  if (EXISTS "${PULSE_JUCE_PATH}/CMakeLists.txt")
    add_subdirectory("${PULSE_JUCE_PATH}" JUCE)
  else()
    message(FATAL_ERROR "JUCE not found. Set JUCE_DIR or PULSE_JUCE_PATH to a local JUCE checkout.")
  endif()
endif()

add_library(pulse
  src/Compiler.cpp
  src/Engine.cpp
  src/Lexer.cpp
  src/Parser.cpp
  src/Runtime.cpp
)

target_include_directories(pulse
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(pulse PUBLIC cxx_std_20)

if (MSVC)
  target_compile_options(pulse PRIVATE /W4 /permissive-)
else()
  target_compile_options(pulse PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(pulse_demo
  src/main.cpp
)

target_link_libraries(pulse_demo PRIVATE pulse)

juce_add_plugin(PulsePlugin
  COMPANY_NAME "matd.space"
  BUNDLE_ID "space.matd.fabric"
  VERSION ${PROJECT_VERSION}
  IS_SYNTH FALSE
  IS_MIDI_EFFECT TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT TRUE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  PLUGIN_MANUFACTURER_CODE MtdS
  PLUGIN_CODE Fbrc
  FORMATS AU Standalone
  PRODUCT_NAME "Fabric"
  AU_MAIN_TYPE kAudioUnitType_MIDIProcessor
)

target_sources(PulsePlugin PRIVATE
  Source/PluginEditor.cpp
  Source/PluginEditor.h
  Source/PluginProcessor.cpp
  Source/PluginProcessor.h
)

target_compile_definitions(PulsePlugin PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
)

target_link_libraries(PulsePlugin
  PRIVATE
    pulse
    juce::juce_audio_utils
    juce::juce_gui_extra
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

juce_generate_juce_header(PulsePlugin)
