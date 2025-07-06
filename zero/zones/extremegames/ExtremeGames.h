#pragma once

#include <zero/MapBase.h>

namespace zero {
namespace eg {

struct ExtremeGames {
  std::vector<MapBase> bases;

  size_t GetBaseFromPosition(Vector2f position) {
    u16 x = (u16)position.x;
    u16 y = (u16)position.y;

    for (size_t i = 0; i < bases.size(); ++i) {
      auto& base = bases[i];

      if (base.bitset.Test(x, y)) return i;
    }

    return -1;
  }
};

}  // namespace eg
}  // namespace zero
