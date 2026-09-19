#pragma once

// ============================================================================
// DECIBEL / MIC LEVEL
// ============================================================================

void micSetup();

// Returns a 0-100 relative loudness value (not calibrated SPL).
int micLoop();
