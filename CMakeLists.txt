cmake_minimum_required(VERSION 3.20)

project(Fabric VERSION 0.2.7 LANGUAGES C CXX)

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

set(FABRIC_PLUGIN_SOURCES
  Source/FabricEditor.cpp
  Source/FabricEditor.h
  Source/FabricProcessor.cpp
  Source/FabricProcessor.h
)

function(add_fabric_plugin target_name bundle_id plugin_code product_name mode_definition)
  juce_add_plugin(${target_name}
    COMPANY_NAME "matd.space"
    BUNDLE_ID "${bundle_id}"
    VERSION ${PROJECT_VERSION}
    IS_SYNTH FALSE
    IS_MIDI_EFFECT TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE MtdS
    PLUGIN_CODE ${plugin_code}
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "${product_name}"
    AU_MAIN_TYPE kAudioUnitType_MIDIProcessor
  )

  target_sources(${target_name} PRIVATE ${FABRIC_PLUGIN_SOURCES})

  target_compile_definitions(${target_name} PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
    ${mode_definition}=1
  )

  target_link_libraries(${target_name}
    PRIVATE
      pulse
      juce::juce_audio_utils
      juce::juce_gui_extra
      juce::juce_osc
    PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_lto_flags
      juce::juce_recommended_warning_flags
  )

  juce_generate_juce_header(${target_name})
endfunction()

add_fabric_plugin(FabricGeneratePlugin "space.matd.fabric.generate" FbGn "Fabric Generate" FABRIC_PLUGIN_MODE_GENERATE)
add_fabric_plugin(FabricProcessPlugin "space.matd.fabric.process" FbPr "Fabric Process" FABRIC_PLUGIN_MODE_PROCESS)
add_fabric_plugin(FabricCapturePlugin "space.matd.fabric.capture" FbCp "Fabric Capture" FABRIC_PLUGIN_MODE_CAPTURE)
add_fabric_plugin(FabricHubPlugin "space.matd.fabric.hub" FbHb "Fabric Hub" FABRIC_PLUGIN_MODE_HUB)
