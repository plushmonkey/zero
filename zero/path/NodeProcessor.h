#pragma once

#include <zero/Types.h>
#include <zero/game/Game.h>
#include <zero/game/Map.h>
#include <zero/path/Node.h>

namespace zero {

struct Game;

namespace path {

constexpr size_t kMaxNodes = 1024 * 1024;

struct NodeConnections {
  Node* neighbors[8];
  size_t count;
};

// Determines the node edges when using A*.
class NodeProcessor {
 public:
  NodeProcessor(Game& game) : game_(game), map_(game.connection.map) {}
  Game& GetGame() { return game_; }

  NodeConnections FindEdges(Node* node, Node* start, Node* goal);
  Node* GetNode(NodePoint point);
  bool IsSolid(u16 x, u16 y) { return map_.IsSolid(x, y, 0xFFFF); }

  // Calculate the node from the index.
  // This lets the node exist without storing its position so it fits in cache better.
  inline NodePoint GetPoint(const Node* node) const {
    size_t index = (node - &nodes_[0]);

    uint16_t world_y = (uint16_t)(index / 1024);
    uint16_t world_x = (uint16_t)(index % 1024);

    return NodePoint(world_x, world_y);
  }

 private:
  Node nodes_[kMaxNodes];
  const Map& map_;
  Game& game_;
};

}  // namespace path
}  // namespace zero
