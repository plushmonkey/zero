#include <zero/game/Game.h>
#include <zero/path/NodeProcessor.h>

namespace zero {
namespace path {

NodeConnections NodeProcessor::FindEdges(Node* node, Node* start, Node* goal, float radius) {
  NodeConnections connections;
  connections.count = 0;

  NodePoint base_point = GetPoint(node);

  Player* self = game_.player_manager.GetSelf();
  u16 freq = self->frequency;

  for (int16_t y = -1; y <= 1; ++y) {
    for (int16_t x = -1; x <= 1; ++x) {
      if (x == 0 && y == 0) continue;

      uint16_t world_x = base_point.x + x;
      uint16_t world_y = base_point.y + y;

      NodePoint current_point(world_x, world_y);
      Node* current = GetNode(current_point);

      if (current != nullptr) {
        if (!(current->flags & NodeFlag_Traversable)) continue;

        if (map_.GetTileId(current_point.x, current_point.y) == kTileSafeId) {
          current->weight = 10.0f;
        }

        connections.neighbors[connections.count++] = current;

        if (connections.count >= 8) {
          return connections;
        }
      }
    }
  }

  return connections;
}

Node* NodeProcessor::GetNode(NodePoint point) {
  if (point.x >= 1024 || point.y >= 1024) {
    return nullptr;
  }

  std::size_t index = point.y * 1024 + point.x;
  Node* node = &nodes_[index];

  if (!(node->flags & NodeFlag_Initialized)) {
    node->parent = nullptr;
    node->flags = NodeFlag_Initialized | (node->flags & NodeFlag_Traversable);
    node->g = node->f = 0.0f;
    node->weight = 1.0f;
  }

  return &nodes_[index];
}

}  // namespace path
}  // namespace zero
