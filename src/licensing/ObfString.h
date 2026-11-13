#pragma once
#include <juce_core/juce_core.h>
#include "Obfuscate.h"

// juce::String-returning obfuscated literal (for use in JUCE expressions:
// comparisons, concatenation, URLs). WSHO_OBF alone yields std::string.
#define WSHO_JS(str) juce::String (WSHO_OBF(str).c_str())
