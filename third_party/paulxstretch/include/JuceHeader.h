#pragma once

/** RUMOR shim: PaulXStretch relative includes mapped to JUCE 8 modules. */
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_cryptography/juce_cryptography.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#if JUCE_MODULE_AVAILABLE_juce_graphics
 #include <juce_graphics/juce_graphics.h>
#endif

using namespace juce;
