#include <zero/game/Game.h>
#include <zero/path/NodeProcessor.h>

namespace zero {
namespace path {

inline bool CanOccupy(const Map& map, Rectangle& rect, Vector2f offset, u32 frequency) {
  Vector2f min = rect.min + offset;
  Vector2f max = rect.max + offset;

  for (u16 y = (u16)min.y; y <= (u16)max.y; ++y) {
    for (u16 x = (u16)min.x; x <= (u16)max.x; ++x) {
      if (map.IsSolid(x, y, frequency)) {
        return false;
      }
    }
  }

  return true;
}

inline bool CanOccupyAxis(const Map& map, Rectangle& rect, Vector2f offset, u32 frequency) {
  Vector2f min = rect.min + offset;
  Vector2f max = rect.max + offset;

  if (offset.x < 0) {
    // Moving west, so check western section of rect
    for (u16 y = (u16)min.y; y <= (u16)max.y; ++y) {
      if (map.IsSolid((u16)min.x, y, frequency)) {
        return false;
      }
    }
  } else if (offset.x > 0) {
    // Moving east, so check eastern section of rect
    for (u16 y = (u16)min.y; y <= (u16)max.y; ++y) {
      if (map.IsSolid((u16)max.x, y, frequency)) {
        return false;
      }
    }
  } else if (offset.y < 0) {
    // Moving north, so check north section of rect
    for (u16 x = (u16)min.x; x <= (u16)max.x; ++x) {
      if (map.IsSolid(x, (u16)min.y, frequency)) {
        return false;
      }
    }
  } else if (offset.y > 0) {
    // Moving south, so check south section of rect
    for (u16 x = (u16)min.x; x <= (u16)max.x; ++x) {
      if (map.IsSolid(x, (u16)max.y, frequency)) {
        return false;
      }
    }
  }

  return true;
}

NodeConnections NodeProcessor::FindEdges(Node* node, Node* start, Node* goal, float radius) {
  NodeConnections connections;
  connections.count = 0;

  NodePoint base_point = GetPoint(node);

  Player* self = game_.player_manager.GetSelf();
  u16 freq = self->frequency;

  NodePoint skip_point(-1, -1);
  bool use_rect = node->parent;
  Rectangle rect;

  // If the node has a parent, calculate an occupy rect to compare movement against.
  // This is used to check diagonal solid tiles.
  if (node->parent) {
    NodePoint parent_point = GetPoint(node->parent);
    Vector2f parent_pos(parent_point.x, parent_point.y);

    skip_point = parent_point;

    Vector2f base_pos(base_point.x, base_point.y);
    OccupyRect o_rect = map_.GetClosestOccupyRect(base_pos, radius, parent_pos);

    if (o_rect.occupy) {
      rect = Rectangle(Vector2f(o_rect.start_x, o_rect.start_y), Vector2f(o_rect.end_x, o_rect.end_y));
    }
  }

  for (int16_t y = -1; y <= 1; ++y) {
    for (int16_t x = -1; x <= 1; ++x) {
      if (x == 0 && y == 0) continue;
      bool diagonal = (x + y) % 2 == 0;

      uint16_t world_x = base_point.x + x;
      uint16_t world_y = base_point.y + y;

      // Skip over the parent node because we just came from that
      if (world_x == skip_point.x && world_y == skip_point.y) continue;

      // Skip over occupy movement if the node is in the current occupied rect.
      bool is_occupied = rect.Contains(Vector2f(world_x, world_y));

      // Perform checks to see if we can move from the node to the next one while taking the parent occupying region into consideration.
      if (use_rect && !is_occupied) {
        if (diagonal) {
          // Separate the occupy checks so it can find collisions on diagonals
          if (x != 0) {
            if (!CanOccupyAxis(map_, rect, Vector2f(x, 0), freq)) {
              continue;
            }
          }

          if (y != 0) {
            if (!CanOccupyAxis(map_, rect, Vector2f(0, y), freq)) {
              continue;
            }
          }
        } else {
          if (!CanOccupy(map_, rect, Vector2f(x, y), freq)) {
            continue;
          }
        }
      }

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
