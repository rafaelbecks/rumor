cmake_minimum_required(VERSION 3.22)
project(RUMOR VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RUMOR_WITH_PAULXSTRETCH "Build PaulXStretch audio engine" ON)
option(RUMOR_BUILD_PLUGIN "Build JUCE VST3/AU/Standalone" ON)
option(RUMOR_BUILD_TESTS "Build unit tests" ON)

set(RUMOR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(RUMOR_SRC "${RUMOR_ROOT}/src")
set(RUMOR_THIRD_PARTY "${RUMOR_ROOT}/third_party")

# --- Platform ---------------------------------------------------------------
if(APPLE)
  find_library(COREFOUNDATION_LIB CoreFoundation REQUIRED)
  find_library(ACCELERATE_LIB Accelerate REQUIRED)
  set(RUMOR_PLATFORM_LIBS ${COREFOUNDATION_LIB} ${ACCELERATE_LIB})
elseif(WIN32)
  set(RUMOR_PLATFORM_LIBS "")
elseif(UNIX)
  find_package(Threads REQUIRED)
  set(RUMOR_PLATFORM_LIBS Threads::Threads ${CMAKE_DL_LIBS} m)
else()
  message(FATAL_ERROR "RUMOR supports macOS, Windows, and Linux.")
endif()

# --- Core granular DSP + serial (from UMBRAL / glow) ------------------------
add_library(rumor_core STATIC
  "${RUMOR_SRC}/audio/Synth.cpp"
  "${RUMOR_SRC}/audio/Oscillator.cpp"
  "${RUMOR_SRC}/audio/Filter.cpp"
  "${RUMOR_SRC}/audio/Granular.cpp"
  "${RUMOR_SRC}/audio/Limiter.cpp"
  "${RUMOR_SRC}/audio/Envelope.cpp"
  "${RUMOR_SRC}/audio/WaveFolder.cpp"
  "${RUMOR_SRC}/parameters/Parameter.cpp"
  "${RUMOR_SRC}/parameters/ParameterRegistry.cpp"
  "${RUMOR_SRC}/control/SerialPitch.cpp"
  "${RUMOR_SRC}/config/ConfigLoader.cpp"
  "${RUMOR_ROOT}/engines/granular/GranularEngine.cpp"
)
target_include_directories(rumor_core PUBLIC
  "${RUMOR_SRC}"
  "${RUMOR_ROOT}"
  "${RUMOR_THIRD_PARTY}/nlohmann_json/single_include"
)
target_link_libraries(rumor_core PUBLIC ${RUMOR_PLATFORM_LIBS})
if(APPLE)
  target_compile_options(rumor_core PRIVATE -Wno-unused-parameter)
endif()

# --- JUCE -------------------------------------------------------------------
set(RUMOR_JUCE_DIR "${RUMOR_ROOT}/deps/JUCE")
if(EXISTS "${RUMOR_JUCE_DIR}/CMakeLists.txt")
  message(STATUS "Using JUCE at ${RUMOR_JUCE_DIR}")
  add_subdirectory("${RUMOR_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
  include(FetchContent)
  set(FETCHCONTENT_BASE_DIR "${RUMOR_ROOT}/deps" CACHE PATH "")
  FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(JUCE)
endif()

# --- PaulXStretch DSP (optional) --------------------------------------------
if(RUMOR_WITH_PAULXSTRETCH)
  add_subdirectory("${RUMOR_THIRD_PARTY}/paulxstretch")
  add_library(rumor_px_engine STATIC
    "${RUMOR_ROOT}/engines/paulxstretch/PaulXStretchEngine.cpp"
  )
  target_include_directories(rumor_px_engine PUBLIC "${RUMOR_ROOT}")
  target_link_libraries(rumor_px_engine PUBLIC rumor_paulxstretch_dsp)
  target_compile_definitions(rumor_px_engine
    PUBLIC
      RUMOR_HAS_PAULXSTRETCH=1
      JUCE_WEB_BROWSER=0
      JUCE_USE_CURL=0
    PRIVATE
      JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
  )
endif()

# --- JUCE plugin / standalone -----------------------------------------------
if(RUMOR_BUILD_PLUGIN)
  set(RUMOR_FORMATS VST3 Standalone)
  if(APPLE)
    list(APPEND RUMOR_FORMATS AU)
  endif()

  juce_add_plugin(Rumor
    COMPANY_NAME "RUMOR"
    BUNDLE_ID com.rumor.instalacion
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Rumr
    PLUGIN_CODE Rums
    FORMATS ${RUMOR_FORMATS}
    PRODUCT_NAME "RUMOR"
    DESCRIPTION "RUMOR interactive installation (UMBRAL + PaulXStretch)"
    VST3_CATEGORIES "Instrument|Synth"
    ICON_BIG "${RUMOR_ROOT}/app/Assets/icon-soterrado-fullbleed.png"
    ICON_SMALL "${RUMOR_ROOT}/app/Assets/icon-soterrado-128.png"
  )

  juce_generate_juce_header(Rumor)

  juce_add_binary_data(RumorAssets
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
      background.png
      app/Assets/fonts/AnaloGrotesk-Trial-Regular.ttf
      app/Assets/fonts/Lato-Thin.ttf
      app/Assets/fonts/Lato-Light.ttf
      app/Assets/fonts/Lato-Regular.ttf
  )

  target_sources(Rumor
    PRIVATE
      app/Source/PluginProcessor.cpp
      app/Source/PluginEditor.cpp
      app/Source/RemoteLink.cpp
      app/Source/RemoteTls.cpp
      app/Source/SensorWs.cpp
      app/Source/SensorConfig.cpp
      app/Source/PaulXStretchPanel.cpp
      app/Source/Presets.cpp
  )

  target_include_directories(Rumor PRIVATE "${RUMOR_ROOT}" "${RUMOR_SRC}")

  target_compile_definitions(Rumor
    PUBLIC
      JUCE_WEB_BROWSER=0
      JUCE_USE_CURL=0
      JUCE_VST3_CAN_REPLACE_VST2=0
      JUCE_DISPLAY_SPLASH_SCREEN=0
      RUMOR_HAS_PAULXSTRETCH=$<BOOL:${RUMOR_WITH_PAULXSTRETCH}>
  )

  set(RUMOR_PLUGIN_LIBS rumor_core juce::juce_audio_utils RumorAssets)
  if(RUMOR_WITH_PAULXSTRETCH)
    list(APPEND RUMOR_PLUGIN_LIBS rumor_px_engine)
  endif()

  if(APPLE)
    list(APPEND RUMOR_PLUGIN_LIBS "-framework Security")
  elseif(WIN32)
    list(APPEND RUMOR_PLUGIN_LIBS secur32 crypt32)
  else()
    find_package(OpenSSL REQUIRED)
    list(APPEND RUMOR_PLUGIN_LIBS OpenSSL::SSL OpenSSL::Crypto)
  endif()

  target_link_libraries(Rumor
    PRIVATE ${RUMOR_PLUGIN_LIBS}
    PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_warning_flags
  )
  # GCC LTO on the 16 GB ARM CI runner exhausts RAM while compiling the plugin
  # and the compiler dies without an error line. Clang and MSVC keep LTO.
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(Rumor PUBLIC juce::juce_recommended_lto_flags)
  endif()
endif()

# --- Tests (granular / serial; no JUCE required) ----------------------------
if(RUMOR_BUILD_TESTS)
  # Serial/mapping core tests (no MIDI device / joystick bridge required).
  add_executable(rumor-tests
    "${RUMOR_THIRD_PARTY}/catch_amalgamated.cpp"
    "${RUMOR_ROOT}/tests/test_serial_pitch.cpp"
  )
  target_include_directories(rumor-tests PRIVATE
    "${RUMOR_SRC}"
    "${RUMOR_THIRD_PARTY}"
    "${RUMOR_THIRD_PARTY}/nlohmann_json/single_include"
  )
  target_link_libraries(rumor-tests PRIVATE rumor_core)
endif()

message(STATUS "RUMOR ${PROJECT_VERSION}")
message(STATUS "  PaulXStretch: ${RUMOR_WITH_PAULXSTRETCH}")
message(STATUS "  Plugin:       ${RUMOR_BUILD_PLUGIN}")
