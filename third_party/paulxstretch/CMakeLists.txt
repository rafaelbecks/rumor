# PaulXStretch DSP subset for RUMOR (PFFFT, JUCE 8).
# Upstream: https://github.com/essej/paulxstretch — see PINNED_COMMIT.txt

add_library(rumor_paulxstretch_dsp STATIC
  PS_Source/globals.cpp
  PS_Source/Stretch.cpp
  PS_Source/ProcessedStretch.cpp
  PS_Source/FreeEdit.cpp
  PS_Source/BinauralBeats.cpp
  PS_Source/StretchSource.cpp
  PS_Source/ps3_BufferingAudioSource.cpp
  WDL/resample.cpp
  pffft/pffft.c
)

target_include_directories(rumor_paulxstretch_dsp
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/PS_Source"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/WDL"
    "${CMAKE_CURRENT_SOURCE_DIR}/pffft"
)

target_compile_definitions(rumor_paulxstretch_dsp
  PUBLIC
    PS_USE_PFFFT=1
    PS_USE_VDSP_FFT=0
    # PUBLIC so consumers (rumor_px_engine) don't compile juce_gui_extra with WebBrowser/GTK.
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
  PRIVATE
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
)

# Upstream DSP uses `#ifdef WINDOWS` (not `_WIN32`) and `M_PI` (MSVC needs _USE_MATH_DEFINES).
if(WIN32)
  target_compile_definitions(rumor_paulxstretch_dsp PRIVATE WINDOWS WIN32 _USE_MATH_DEFINES)
endif()

target_compile_features(rumor_paulxstretch_dsp PUBLIC cxx_std_17)

target_link_libraries(rumor_paulxstretch_dsp
  PUBLIC
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_core
    juce::juce_cryptography
    juce::juce_data_structures
    juce::juce_events
)

if(APPLE)
  target_compile_options(rumor_paulxstretch_dsp PRIVATE
    -Wno-unused-parameter
    -Wno-deprecated-declarations
    -Wno-float-equal
  )
elseif(WIN32)
  # Avoid min/max macros from windows.h colliding with std::min/max in DSP headers.
  target_compile_definitions(rumor_paulxstretch_dsp PRIVATE NOMINMAX)
elseif(UNIX)
  target_link_libraries(rumor_paulxstretch_dsp PUBLIC m)
  target_compile_options(rumor_paulxstretch_dsp PRIVATE -Wno-unused-parameter)
endif()

message(STATUS "RUMOR PaulXStretch DSP (PFFFT) from ${CMAKE_CURRENT_SOURCE_DIR}")
