#ifndef HR_LAYOUT_H
#define HR_LAYOUT_H

// ============================================================================
// Geometry for the BPM + heart block (the middle row of the panel)
// ============================================================================
//
// The companion to steps_layout.h, and split out for the same reason: this is
// the arithmetic part of that row's layout, so it is the part a host test can
// check without Adafruit GFX or a live panel. The drawing code and tests/
// share one definition instead of each carrying a copy.
//
// Deliberately NOT here: the BPM's rendered width. That comes from GFX's
// getTextBounds() for the font styles and from cell arithmetic for the
// seven-segment style, and is passed in — same contract as the label width in
// steps_layout.h.

// Blank columns between the number and the heart. Three, which is what the
// old fixed x36/x40 pair happened to leave, and it looked right: one or two
// and the heart crowds the ones digit, more and the pair stops reading as one
// unit.
const int HR_GAP = 3;

// Horizontal extent of drawHeart() at a given scale: the triangle spans
// cx - 2*scale .. cx + 2*scale inclusive, and the lobes (radius `scale`,
// centred a scale either side of cx) sit inside that.
inline int hrHeartWidth(int scale) { return scale * 4 + 1; }

// Total width of the number, the gap, and the heart together.
inline int hrBlockWidth(int numW, int heartW) { return numW + HR_GAP + heartW; }

// Left column of the centred block.
//
// Clamped at 0 for the same reason stepsBlockX() is: a block too wide for the
// panel degrades to left-aligned and clips on the right, rather than handing
// GFX a negative cursor and wrapping.
inline int hrBlockX(int numW, int heartW, int panelW) {
  int x = (panelW - hrBlockWidth(numW, heartW)) / 2;
  return x < 0 ? 0 : x;
}

// Last column of the number: what printRightAligned() takes as its
// `rightEdge`. The number stays right-aligned WITHIN the block, so the digits
// still grow leftwards from the heart.
inline int hrNumRightX(int blockX, int numW) { return blockX + numW - 1; }

// Centre column of the heart, which is what drawHeart() takes: HR_GAP clear
// of the number, then half a heart in.
inline int hrHeartCX(int blockX, int numW, int scale) {
  return blockX + numW + HR_GAP + scale * 2;
}

#endif // HR_LAYOUT_H
