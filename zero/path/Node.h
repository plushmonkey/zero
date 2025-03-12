#pragma once

#include <zero/Types.h>

namespace zero {
namespace path {

struct NodePoint {
  uint16_t x;
  uint16_t y;

  NodePoint() : x(0), y(0) {}
  NodePoint(uint16_t x, uint16_t y) : x(x), y(y) {}

  bool operator==(const NodePoint& other) const { return x == other.x && y == other.y; }
};

enum NodeFlag {
  NodeFlag_Openset = (1 << 0),
  NodeFlag_Touched = (1 << 1),
  NodeFlag_Initialized = (1 << 2),
  NodeFlag_Traversable = (1 << 3),
  NodeFlag_Safety = (1 << 4),
  // Brick is handled here instead of checking map to improve performance.
  // If it checked the map then it would be going all over the place with memory touches.
  NodeFlag_Brick = (1 << 5),
};
typedef u32 NodeFlags;

struct Node {
  u32 parent_id;

  float g;
  float f;

  u8 flags;

 private:
  // Fixed point weight where every 10 is 1
  u8 weight;

 public:
  Node() : flags(0), parent_id(~0), g(0.0f), f(0.0f), weight(10) {}

  inline float GetWeight() const { return weight / 10.0f; }
  inline void SetWeight(float v) {
    u32 calc = (u32)(v * 10.0f);

    if (calc <= 255) {
      weight = (u8)(calc);
    } else {
      weight = 255;
    }
  }
};

}  // namespace path
}  // namespace zero
