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

enum { NodeFlag_Openset = (1 << 0), NodeFlag_Closed = (1 << 1), NodeFlag_Initialized = (1 << 2) };
typedef u32 NodeFlags;

struct Node {
  Node* parent;

  float g;
  float f;

  float weight;

  u8 flags;

  Node() : flags(0), parent(nullptr), g(0.0f), f(0.0f), weight(1.0f) {}
};

}  // namespace path
}  // namespace zero
