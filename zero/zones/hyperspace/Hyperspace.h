#pragma once

#include <zero/Math.h>
#include <zero/RegionRegistry.h>

namespace zero {
namespace hyperspace {

// A position within each sector from 1 to 8, center, then warp ring.
// To be used with the region registry to find out which region a coord is connected to.
constexpr Vector2f kSectorPositions[] = {Vector2f(800, 350), Vector2f(800, 475), Vector2f(800, 735), Vector2f(500, 735),
                                         Vector2f(150, 735), Vector2f(150, 500), Vector2f(150, 350), Vector2f(620, 280),
                                         Vector2f(512, 512), Vector2f(50, 50)};

constexpr size_t kCenterSectorIndex = 8;
constexpr size_t kWarpRingSectorIndex = 9;

// A list of warpgate connections.
// First position is always from the outside ring.
// This should stay in the order of kSectorPositions so simple mappings can be done.
constexpr Vector2f kWarpgateConnections[][2] = {
    {Vector2f(960, 60), Vector2f(890, 355)},   // Sector 1
    {Vector2f(960, 675), Vector2f(770, 500)},  // Sector 2
    {Vector2f(960, 960), Vector2f(670, 850)},  // Sector 3
    {Vector2f(512, 960), Vector2f(610, 755)},  // Sector 4
    {Vector2f(60, 960), Vector2f(150, 895)},   // Sector 5
    {Vector2f(60, 675), Vector2f(260, 460)},   // Sector 6
    {Vector2f(60, 60), Vector2f(135, 390)},    // Sector 7
    {Vector2f(512, 60), Vector2f(620, 250)},   // Sector 8
    {Vector2f(60, 350), Vector2f(390, 395)},   // CenterTopLeft
    {Vector2f(960, 350), Vector2f(570, 675)},  // CenterBottomRight
};

// This returns the sector index from kSectorPositions array.
inline int GetSectorFromPosition(RegionRegistry& registry, Vector2f position) {
  MapCoord pos_coord = position;

  for (size_t i = 0; i < ZERO_ARRAY_SIZE(kSectorPositions); ++i) {
    if (registry.IsConnected(kSectorPositions[i], pos_coord)) {
      return (int)i;
    }
  }

  return -1;
}

}  // namespace hyperspace
}  // namespace zero
