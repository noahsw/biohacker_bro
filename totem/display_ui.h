#pragma once

// ============================================================================
// Totem stand-in for the chest display's display_ui.h
// ============================================================================
//
// brightness.cpp (shared, symlinked) talks to "display" and draws its level
// pips through showBrightnessIndicator(). On the totem both go to FastLED, so
// the thumb wheel works here with no change to brightness.cpp.

#include <stdint.h>

struct TotemDisplay {
  void setBrightness8(uint8_t b);
};
extern TotemDisplay *display;

void showBrightnessIndicator(int level, int count, unsigned long untilMs);
