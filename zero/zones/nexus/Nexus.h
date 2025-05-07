#pragma once

#include <zero/Math.h>
#include <zero/path/Path.h>

#include <bitset>
#include <memory>
#include <vector>

#define TW_RENDER_FR 0

namespace zero {
namespace nexus {

	struct RegionBitset {
  std::bitset<1024 * 1024> data;

  inline bool Test(Vector2f position) const { return Test((u16)position.x, (u16)position.y); }

  inline bool Test(u16 x, u16 y) const {
    if (x > 1023 || y > 1023) return false;
    return data.test((size_t)y * (size_t)1024 + (size_t)x);
  }

  inline void Set(Vector2f position, bool val = true) { Set((u16)position.x, (u16)position.y, val); }

  inline void Set(u16 x, u16 y, bool val = true) {
    if (x > 1023 || y > 1023) return;
    data.set((size_t)y * (size_t)1024 + (size_t)x, val);
  }

  inline void Clear() { data.reset(); }
};

struct Nexus {
  // Bitset representing every tile that is considered part of the flagroom.
  RegionBitset fr_bitset;
  // The average position of the flags, which should be the middle flag.
  Vector2f flag_position;
  // The middle corridor leading into the flagroom. This is used for some behavior checks, such as pathfinding from it
  // to find safe paths through the flagroom.
  Vector2f entrance_position;

  path::Path left_entrance_path;
  path::Path right_entrance_path;

  std::vector<Vector2f> corridors;
#if TW_RENDER_FR
  std::vector<Vector2f> fr_positions;
#endif
};

}  // namespace nexus
}  // namespace zero