#pragma once

#include <zero/Math.h>

#include <bitset>
#include <memory>
#include <vector>

#define TW_RENDER_FR 0

namespace zero {
namespace tw {

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

struct TrenchWars {
  RegionBitset fr_bitset;
  Vector2f flag_position;

#if TW_RENDER_FR
  std::vector<Vector2f> fr_positions;
#endif
};

}  // namespace tw
}  // namespace zero
