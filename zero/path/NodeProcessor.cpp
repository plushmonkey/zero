#include <zero/game/Game.h>
#include <zero/game/Logger.h>
#include <zero/path/NodeProcessor.h>

namespace zero {
namespace path {

static inline bool IsDynamicTile(const Map& map, u16 world_x, u16 world_y) {
  TileId tile_id = map.GetTileId(world_x, world_y);

  // Include 1 additional tile in the last door id span to include any door that might currently be empty on creation.
  return tile_id >= kTileIdFirstDoor && tile_id <= (kTileIdLastDoor + 1);
}

bool NodeProcessor::UpdateDynamicNode(Node* node, float ship_radius, u16 frequency) {
  if (!(node->flags & NodeFlag_DynamicEmpty)) {
    return node->flags & NodeFlag_Traversable;
  }

  NodePoint node_point = this->GetPoint(node);
  Vector2f node_position((float)node_point.x, (float)node_point.y);
  size_t node_index = (size_t)node_point.y * 1024 + node_point.x;
  MemoryArena& arena = GetGame().temp_arena;
  const Map& map = GetGame().GetMap();
  MemoryRevert reverter = arena.GetReverter();

  OccupiedRect* rects = memory_arena_push_type_count(&arena, OccupiedRect, 256);

  size_t rect_count =
      map.GetAllOccupiedRects(Vector2f((float)node_point.x, (float)node_point.y), ship_radius, frequency, rects, true);

  // Clear the dynamic flag so we don't have to update this again until doors change. Also make it non-traversable.
  node->flags &= ~(NodeFlag_DynamicEmpty | NodeFlag_Traversable);

  EdgeSet new_set = {};
  edges_[node_index] = new_set;

  // If there's only two and the two are offset from each other, then we must be on a diagonal tile and should not
  // proceed.
  if (rect_count == 2 && rects[0].start_x != rects[1].start_x && rects[0].start_y != rects[1].start_y) {
    return false;
  }

  // Check if we can even occupy this location.
  for (size_t i = 0; i < rect_count; ++i) {
    OccupiedRect* rect = rects + i;

    if (rect->Contains(node_position)) {
      node->flags |= NodeFlag_Traversable;
      break;
    }
  }

  if (!(node->flags & NodeFlag_Traversable)) return false;

  static const CoordOffset neighbors[4] = {CoordOffset::North(), CoordOffset::South(), CoordOffset::West(),
                                           CoordOffset::East()};

  // Recompute EdgeSet.
  for (size_t i = 0; i < 4; ++i) {
    uint16_t world_x = node_point.x + neighbors[i].x;
    uint16_t world_y = node_point.y + neighbors[i].y;

    // If we are smaller than 1 tile, then we need to do solid checks for neighbors.
    if (ship_radius <= 0.5f) {
      if (map_.IsSolidEmptyDoors(world_x, world_y, 0xFFFF)) {
        continue;
      }
    } else {
      bool is_occupied = false;
      // Check each occupied rect to see if contains the target position.
      // The expensive check can be skipped because this spot is definitely occupiable.
      for (size_t j = 0; j < rect_count; ++j) {
        OccupiedRect& rect = rects[j];

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

    new_set.Set(i);

    if (IsDynamicTile(map_, world_x, world_y)) {
      new_set.DynamicSet(i);
    }
  }

  edges_[node_index] = new_set;

  return node->flags & NodeFlag_Traversable;
}

EdgeSet NodeProcessor::FindEdges(Node* node, float radius) {
  NodePoint point = GetPoint(node);
  size_t index = (size_t)point.y * 1024 + point.x;
  EdgeSet edges = this->edges_[index];

  for (size_t i = 0; i < 8; ++i) {
    // Only check if the tile is dynamic, like doors.
    if (!edges.DynamicIsSet(i)) continue;

    switch (door_method_) {
      case DoorSolidMethod::AlwaysOpen: {
        // Do nothing so the edge stays.
      } break;
      case DoorSolidMethod::AlwaysSolid: {
        // Remove the edge because it's always treated as solid.
        edges.Erase(i);
      } break;
      case DoorSolidMethod::Dynamic: {
        // Perform a solid check here to make sure doors haven't blocked us.
        CoordOffset offset = CoordOffset::FromIndex(i);
        if (map_.IsSolid(point.x + offset.x, point.y + offset.y, 0xFFFF)) {
          edges.Erase(i);
        }
      } break;
      default: {
      } break;
    }
  }

  return edges;
}

EdgeSet NodeProcessor::CalculateEdges(Node* node, float radius, OccupiedRect* occupied_scratch) {
  EdgeSet edges = {};

  NodePoint base_point = GetPoint(node);

  bool north = false;
  bool south = false;

  bool* setters[8] = {&north, &south, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
  bool* requirements[8] = {nullptr, nullptr, nullptr, nullptr, &north, &north, &south, &south};
  static const CoordOffset neighbors[8] = {CoordOffset::North(),     CoordOffset::South(),     CoordOffset::West(),
                                           CoordOffset::East(),      CoordOffset::NorthWest(), CoordOffset::NorthEast(),
                                           CoordOffset::SouthWest(), CoordOffset::SouthEast()};

  size_t occupied_count =
      map_.GetAllOccupiedRects(Vector2f((float)base_point.x, (float)base_point.y), radius, 0xFFFF, occupied_scratch);

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
        OccupiedRect& rect = occupied_scratch[j];

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
