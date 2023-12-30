#pragma once

#include <zero/Math.h>

#include <algorithm>

namespace zero {

struct InfluenceMap {
  InfluenceMap() {
    tiles = new float[1024 * 1024];
    Clear();
  }

  float GetValue(u16 x, u16 y) { return tiles[y * 1024 + x]; }

  float GetValue(Vector2f v) { return tiles[(u16)v.y * 1024 + (u16)v.x]; }

  void AddValue(u16 x, u16 y, float value) { tiles[y * 1024 + x] += value; }

  void SetValue(u16 x, u16 y, float value) { tiles[y * 1024 + x] = value; }

  void Clear() {
    for (size_t i = 0; i < 1024 * 1024; ++i) {
      tiles[i] = 0.0f;
    }
  }

  void Update(float dt) {
    for (size_t i = 0; i < 1024 * 1024; ++i) {
      tiles[i] = std::max(0.0f, tiles[i] - dt);
    }
  }

  float* tiles;
};

}  // namespace zero
