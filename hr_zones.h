#pragma once

// ============================================================================
// HR ZONE MATH — pure, testable, no hardware
// ============================================================================
//
// Deliberately free of <Arduino.h>, the HUB75 driver, and any global display
// state, so it compiles and runs on a normal machine under tests/. Everything
// here is a pure function of a BPM number.
//
// This is the part of the display that can lie to you without looking broken:
// a wrong bar length is still a plausible-looking bar. It's also the part most
// likely to be edited, since the zone thresholds in config.h are expected to
// need retuning after real wear — so the tests exist mainly to catch a retune
// that silently breaks the scale.

#include "config.h"

// Zones are numbered 0..ZONE_COUNT-1 (Z4 covers "zone 4 and 5" together).
const int ZONE_COUNT = 5;

// Lower bound of each zone in BPM, for zone 0..ZONE_COUNT. The last entry is
// the ceiling of the top zone, so zoneFloorBpm(z)..zoneFloorBpm(z+1) is always
// zone z's range. Note zone 0 starts at REST_BPM, not 0 — see config.h.
int zoneFloorBpm(int zone);

// Which zone a BPM falls in. Clamps: anything below REST_BPM is zone 0.
int hrZone(int bpm);

// How far along the WHOLE bar this BPM sits, 0.0 to 1.0.
//
// Piecewise-linear, not linear in BPM: each zone owns exactly 1/ZONE_COUNT of
// the width, and position inside that slice is progress through that zone's
// own BPM range. That's what makes "am I about to tip into the next zone?"
// readable at a glance, and it's why this can't just be a map() call.
float barFraction(int bpm);

// Left edge, in pixels, of zone `zone`'s slice of a bar `panelWidth` wide.
// Derived rather than hardcoded so the width doesn't quietly round away:
// 64/5 = 12.8px per zone, so slices come out 12 or 13 wide and still tile
// the row exactly, with no gap and no overlap.
int zoneSliceX(int zone, int panelWidth);
