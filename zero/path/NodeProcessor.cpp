#include <zero/game/Game.h>
#include <zero/game/Logger.h>
#include <zero/path/NodeProcessor.h>

namespace zero {
namespace path {

EdgeSet NodeProcessor::FindEdges(Node* node, float radius) {
  NodePoint point = GetPoint(node);
  size_t index = (size_t)point.y * 1024 + point.x;
  EdgeSet edges = this->edges_[index];

  for (size_t i = 0; i < 8; ++i) {
    // Only check if the tile is dynamic, like doors.
    if (!edges.DynamicIsSet(i)) continue;

    // Perform a solid check here to make sure doors haven't blocked us.
    CoordOffset offset = CoordOffset::FromIndex(i);
    if (map_.IsSolid(point.x + offset.x, point.y + offset.y, 0xFFFF)) {
      edges.Erase(i);
    }
  }

  return edges;
}

static inline bool CanOccupy(const Map& map, OccupiedRect& rect, Vector2f offset) {
  Vector2f min = Vector2f(rect.start_x, rect.start_y) + offset;
  Vector2f max = Vector2f(rect.end_x, rect.end_y) + offset;

  u32 frequency = 0xFFFF;

  for (u16 y = (u16)min.y; y <= (u16)max.y; ++y) {
    for (u16 x = (u16)min.x; x <= (u16)max.x; ++x) {
      if (map.IsSolidEmptyDoors(x, y, frequency)) {
        return false;
      }
    }
  }

  return true;
}

static inline bool CanOccupyAxis(const Map& map, OccupiedRect& rect, Vector2f offset) {
  Vector2f min = Vector2f(rect.start_x, rect.start_y) + offset;
  Vector2f max = Vector2f(rect.end_x, rect.end_y) + offset;

  u32 frequency = 0xFFFF;

  if (offset.x < 0) {
    // Moving west, so check western section of rect
    for (u16 y = (u16)min.y; y <= (u16)max.y; ++y) {
      if (map.IsSolidEmptyDoors((u16)min.x, y, frequency)) {
        return false;
      }
    }
  } else if (offset.x > 0) {
    // Moving east, so check eastern section of rect
    for (u16 y = (u16)min.y; y <= (u16)max.y; ++y) {
      if (map.IsSolidEmptyDoors((u16)max.x, y, frequency)) {
        return false;
      }
    }
  } else if (offset.y < 0) {
    // Moving north, so check north section of rect
    for (u16 x = (u16)min.x; x <= (u16)max.x; ++x) {
      if (map.IsSolidEmptyDoors(x, (u16)min.y, frequency)) {
        return false;
      }
    }
  } else if (offset.y > 0) {
    // Moving south, so check south section of rect
    for (u16 x = (u16)min.x; x <= (u16)max.x; ++x) {
      if (map.IsSolidEmptyDoors(x, (u16)max.y, frequency)) {
        return false;
      }
    }
  }

  return true;
}

static inline bool IsDynamicTile(const Map& map, u16 world_x, u16 world_y) {
  TileId tile_id = map.GetTileId(world_x, world_y);

  // Include 1 additional tile in the last door id span to include any door that might currently be empty on creation.
  return tile_id >= kFirstDoorId && tile_id <= (kLastDoorId + 1);
}

EdgeSet NodeProcessor::CalculateEdges(Node* node, float radius) {
  EdgeSet edges = {};

  NodePoint base_point = GetPoint(node);

  bool north = false;
  bool south = false;

  bool* setters[8] = {&north, &south, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
  bool* requirements[8] = {nullptr, nullptr, nullptr, nullptr, &north, &north, &south, &south};
  static const CoordOffset neighbors[8] = {CoordOffset::North(),     CoordOffset::South(),     CoordOffset::West(),
                                           CoordOffset::East(),      CoordOffset::NorthWest(), CoordOffset::NorthEast(),
                                           CoordOffset::SouthWest(), CoordOffset::SouthEast()};

  OccupiedRect occupied[64];
  size_t occupied_count =
      map_.GetAllOccupiedRects(Vector2f((float)base_point.x, (float)base_point.y), radius, 0xFFFF, occupied);

  for (std::size_t i = 0; i < 4; i++) {
    bool* requirement = requirements[i];

    if (requirement && !*requirement) continue;

    uint16_t world_x = base_point.x + neighbors[i].x;
    uint16_t world_y = base_point.y + neighbors[i].y;

    // If we are smaller than 1 tile, then we need to do solid checks for neighbors.
    if (radius <= 0.5f) {
      if (map_.IsSolidEmptyDoors(world_x, world_y, 0xFFFF)) {
        continue;
      }
    } else {
      bool is_occupied = false;
      // Check each occupied rect to see if contains the target position.
      // The expensive check can be skipped because this spot is definitely occupiable.
      for (size_t j = 0; j < occupied_count; ++j) {
        OccupiedRect& rect = occupied[j];

        if (rect.Contains(Vector2f((float)world_x, (float)world_y))) {
          is_occupied = true;
          break;
        }
      }

      if (!is_occupied) {
        continue;
      }
    }

    NodePoint current_point(world_x, world_y);
    Node* current = GetNode(current_point);

    if (!current) continue;
    if (!(current->flags & NodeFlag_Traversable)) continue;

    edges.Set(i);

    if (IsDynamicTile(map_, world_x, world_y)) {
      edges.DynamicSet(i);
    }

    if (setters[i]) {
      *setters[i] = true;
    }
  }

  return edges;
}

Node* NodeProcessor::GetNode(NodePoint point) {
  if (point.x >= 1024 || point.y >= 1024) {
    return nullptr;
  }

  std::size_t index = point.y * 1024 + point.x;
  Node* node = &nodes_[index];

  if (!(node->flags & NodeFlag_Initialized)) {
    node->parent_id = ~0;
    // Set the node as initialized and clear openset/touched while keeping any other flags set.
    node->flags = NodeFlag_Initialized | (node->flags & ~(NodeFlag_Openset | NodeFlag_Touched));
    node->g = node->f = 0.0f;
  }

  return &nodes_[index];
}

}  // namespace path
}  // namespace zero
