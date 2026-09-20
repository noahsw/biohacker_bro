#include "hr_zones.h"

namespace {

const int zoneFloor[ZONE_COUNT + 1] = {
  REST_BPM, ZONE1_BPM, ZONE2_BPM, ZONE3_BPM, ZONE4_BPM, ZONE_MAX_BPM
};

} // namespace

int zoneFloorBpm(int zone) {
  if (zone < 0) zone = 0;
  if (zone > ZONE_COUNT) zone = ZONE_COUNT;
  return zoneFloor[zone];
}

int hrZone(int bpm) {
  if (bpm < ZONE1_BPM) return 0;
  if (bpm < ZONE2_BPM) return 1;
  if (bpm < ZONE3_BPM) return 2;
  if (bpm < ZONE4_BPM) return 3;
  return 4;
}

float barFraction(int bpm) {
  int z = hrZone(bpm);
  int lo = zoneFloor[z];
  int hi = zoneFloor[z + 1];
  float within = (float)(bpm - lo) / (float)(hi - lo);
  if (within < 0.0f) within = 0.0f; // below REST_BPM: empty bar
  if (within > 1.0f) within = 1.0f; // above ZONE_MAX_BPM: full bar
  return (z + within) / (float)ZONE_COUNT;
}

int zoneSliceX(int zone, int panelWidth) {
  return (panelWidth * zone) / ZONE_COUNT;
}
