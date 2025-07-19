#pragma once

#include <zero/MapBase.h>

namespace zero {
namespace eg {

struct ExtremeGames {
  std::vector<MapBase> bases;

  size_t GetBaseFromPosition(Vector2f position) {
    struct Coord {
      u16 x;
      u16 y;
      Coord(u16 x, u16 y) : x(x), y(y) {}
    };
    constexpr float kRadius = 14.0f / 16.0f;

    // Check surrounding us so standing on a diagonal-tile won't cause us to think we aren't in a base.
    Coord check_coords[] = {
        Coord((u16)(position.x - kRadius), (u16)(position.y - kRadius)),
        Coord((u16)(position.x + kRadius), (u16)(position.y - kRadius)),
        Coord((u16)(position.x - kRadius), (u16)(position.y + kRadius)),
        Coord((u16)(position.x + kRadius), (u16)(position.y + kRadius)),
    };

    for (size_t i = 0; i < bases.size(); ++i) {
      auto& base = bases[i];

      for (Coord check : check_coords) {
        if (base.bitset.Test(check.x, check.y)) return i;
      }
    }

    return -1;
  }
};

}  // namespace eg
}  // namespace zero
