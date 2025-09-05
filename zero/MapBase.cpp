#include "MapBase.h"

#include <bitset>
#include <deque>
#include <optional>

namespace zero {

using WalledBitset = std::bitset<1024 * 1024>;

constexpr float kShipRadius = 14.0f / 16.0f;

static std::vector<MapCoord> DetectFlagroomPositions(path::Pathfinder& pathfinder, const MapBuildConfig& cfg);
static std::vector<MapBase> BuildBases(const std::vector<MapCoord>& flagrooms, path::Pathfinder& pathfinder,
                                       const MapBuildConfig& cfg);
static Vector2f FloodFillRegion(path::Pathfinder& pathfinder, const WalledBitset& walled_bitset, RegionBitset& region,
                                MapCoord start, std::optional<int> range, RegionDataMap<u16>* depth_map);

std::vector<MapBase> FindBases(path::Pathfinder& pathfinder, const MapBuildConfig& cfg) {
  auto flagroom_positions = DetectFlagroomPositions(pathfinder, cfg);
  return BuildBases(flagroom_positions, pathfinder, cfg);
}

static std::vector<MapCoord> DetectFlagroomPositions(path::Pathfinder& pathfinder, const MapBuildConfig& cfg) {
  using namespace path;

  const auto& map = pathfinder.GetProcessor().GetGame().GetMap();

  struct Node {
    Node* prev = nullptr;
    float dist = 1024.0f * 1024.0f;
    unsigned int open : 1;
    unsigned int visited : 1;
    unsigned int padding : 30;

    Node() : open(0), visited(0) {}
  };

  struct NodeCompare {
    bool operator()(const Node* lhs, const Node* rhs) const { return lhs->dist > rhs->dist; }
  };

  Node* nodes = new Node[1024 * 1024];

  PriorityQueue<Node*, NodeCompare> q;

  nodes[cfg.spawn.y * 1024 + cfg.spawn.x].dist = 0.0f;

  q.Push(nodes + (cfg.spawn.y * 1024 + cfg.spawn.x));

  while (!q.Empty()) {
    Node* node = q.Pop();

    node->open = 0;
    node->visited = 1;

    size_t node_index = (node - &nodes[0]);
    MapCoord node_coord((u16)(node_index % 1024), (u16)(node_index / 1024));

    CoordOffset neighbors[] = {
        CoordOffset::North(),
        CoordOffset::South(),
        CoordOffset::West(),
        CoordOffset::East(),
    };

    bool updated_neighbor = false;

    EdgeSet edgeset = pathfinder.GetProcessor().FindEdges(
        pathfinder.GetProcessor().GetNode(NodePoint(node_coord.x, node_coord.y)), kShipRadius);

    for (size_t i = 0; i < 4; ++i) {
      MapCoord neighbor_coord = node_coord;

      neighbor_coord.x += neighbors[i].x;
      neighbor_coord.y += neighbors[i].y;

      if (neighbor_coord.x > 1023 || neighbor_coord.y > 1023) continue;
      if (!edgeset.IsSet(i)) continue;
      if (map.IsSolid(neighbor_coord.x, neighbor_coord.y, 0xFFFF)) continue;

      Node* neighbor = nodes + neighbor_coord.y * 1024 + neighbor_coord.x;
      float dist = node->dist + 1.0f;

      if (!neighbor->visited) {
        if (!neighbor->open) {
          neighbor->prev = node;
          neighbor->dist = dist;
          neighbor->open = true;

          q.Push(neighbor);
        } else if (dist < neighbor->dist) {
          neighbor->dist = dist;
          neighbor->prev = node;
          updated_neighbor = true;
        }
      }
    }

    // Do the update once at the end
    if (updated_neighbor) {
      q.Update();
    }
  }

  struct Base {
    float distance_delta;
    Vector2f position;
  };

  Vector2f spawn_pos((float)cfg.spawn.x, (float)cfg.spawn.y);

  const auto& get_node = [&](u16 x, u16 y) -> Node* {
    size_t node_index = (size_t)y * (size_t)1024 + (size_t)x;
    return nodes + node_index;
  };

  constexpr float kIgnoreBasesDistanceSq = 125.0f * 125.0f;

  std::vector<Base> bases;

  for (size_t i = 0; i < cfg.base_count; ++i) {
    bases.emplace_back();
    Base& newest_base = bases.back();

    for (u16 y = 0; y < 1024; ++y) {
      for (u16 x = 0; x < 1024; ++x) {
        Vector2f node_position((float)x, (float)y);
        size_t node_index = (size_t)y * (size_t)1024 + (size_t)x;
        Node* node = nodes + node_index;
        if (!node->visited) continue;

        bool can_use_node = true;

        for (size_t j = 0; j < i; ++j) {
          float distance_sq = bases[j].position.DistanceSq(node_position);
          if (distance_sq <= kIgnoreBasesDistanceSq) {
            can_use_node = false;
            break;
          }
        }

        if (!can_use_node) continue;

        float direct_distance = node_position.Distance(spawn_pos);
        // Compute how different the path to the position is from direct distance.
        // This gets us the path that winds the most.
        float distance_delta = (float)node->dist - direct_distance;

        if (distance_delta > newest_base.distance_delta) {
          newest_base.distance_delta = distance_delta;
          newest_base.position = node_position;
        }
      }
    }
  }

  std::vector<MapCoord> result;

  for (auto& base : bases) {
    result.emplace_back((u16)base.position.x, (u16)base.position.y);
  }

  return result;
}

static std::vector<MapBase> BuildBases(const std::vector<MapCoord>& flagrooms, path::Pathfinder& pathfinder,
                                       const MapBuildConfig& cfg) {
  using namespace path;

  const auto& map = pathfinder.GetProcessor().GetGame().GetMap();
  Vector2f spawn_pos((float)cfg.spawn.x, (float)cfg.spawn.y);

  auto walled_bitset = std::make_unique<WalledBitset>();

  const CoordOffset kDirections[] = {
      CoordOffset::East(),      CoordOffset::South(),     CoordOffset::West(),      CoordOffset::North(),
      CoordOffset::NorthWest(), CoordOffset::NorthEast(), CoordOffset::SouthWest(), CoordOffset::SouthEast(),
  };

  for (u16 y = 0; y < 1024; ++y) {
    for (u16 x = 0; x < 1024; ++x) {
      bool near_wall = false;

      for (const auto& direction : kDirections) {
        for (size_t i = 0; i < cfg.empty_exit_range; ++i) {
          s16 offset_x = (s16)(direction.x * i);
          s16 offset_y = (s16)(direction.y * i);

          if (map.IsSolid(x + offset_x, y + offset_y, 0xFFFF)) {
            near_wall = true;
            break;
          }
        }
        if (near_wall) break;
      }

      walled_bitset->set(y * 1024 + x, near_wall);
    }
  }

  std::vector<MapBase> bases;

  for (const auto& fr_coord : flagrooms) {
    bases.emplace_back();
    MapBase& base = bases.back();

    if (cfg.populate_flood_map) {
      base.entrance_position = FloodFillRegion(pathfinder, *walled_bitset, base.bitset, fr_coord, std::nullopt, &base.path_flood_map);
    } else {
      base.entrance_position = FloodFillRegion(pathfinder, *walled_bitset, base.bitset, fr_coord, std::nullopt, nullptr);
    }
    FloodFillRegion(pathfinder, *walled_bitset, base.flagroom_bitset, fr_coord, cfg.flagroom_size, nullptr);
    base.flagroom_position.x = (float)fr_coord.x;
    base.flagroom_position.y = (float)fr_coord.y;
  }

  return bases;
}

static Vector2f FloodFillRegion(path::Pathfinder& pathfinder, const WalledBitset& walled_bitset, RegionBitset& region,
                                MapCoord start, std::optional<int> range, RegionDataMap<u16>* depth_map) {
  using namespace path;

  struct FloodState {
    MapCoord coord;
    int depth;

    FloodState(MapCoord coord, int depth) : coord(coord), depth(depth) {}
  };

  region.Fit(0, 1023, 0, 1023, false);

  if (depth_map) {
    *depth_map = RegionDataMap<u16>(0, 0, 1023, 1023);
  }

  std::deque<FloodState> stack;

  stack.emplace_back(start, 0);
  region.Set(start.x, start.y, true);

  Vector2f entrance_position((float)start.x, (float)start.y);

  while (!stack.empty()) {
    FloodState current = stack.front();
    MapCoord coord = current.coord;

    stack.pop_front();

    if (depth_map) {
      depth_map->Set(coord.x, coord.y, current.depth);
    }

    size_t global_index = (size_t)coord.y * (size_t)1024 + (size_t)coord.x;

    // Either stop at provided range or stop when we reach the entrance.
    if (range) {
      if (current.depth >= *range) continue;
    } else {
      // If we aren't near walls then we must be at the entrance, so stop progressing.
      if (!walled_bitset.test(global_index)) {
        entrance_position.x = (float)coord.x;
        entrance_position.y = (float)coord.y;
        break;
      }
    }

    auto node = pathfinder.GetProcessor().GetNode(NodePoint(coord.x, coord.y));
    EdgeSet edgeset = pathfinder.GetProcessor().FindEdges(node, kShipRadius);

    MapCoord west(coord.x - 1, coord.y);
    MapCoord east(coord.x + 1, coord.y);
    MapCoord north(coord.x, coord.y - 1);
    MapCoord south(coord.x, coord.y + 1);

    if (edgeset.IsSet(path::CoordOffset::WestIndex()) && !region.Test(west.x, west.y)) {
      stack.emplace_back(west, current.depth + 1);
      region.Set(west.x, west.y, true);
    }

    if (edgeset.IsSet(path::CoordOffset::EastIndex()) && !region.Test(east.x, east.y)) {
      stack.emplace_back(east, current.depth + 1);
      region.Set(east.x, east.y, true);
    }

    if (edgeset.IsSet(path::CoordOffset::NorthIndex()) && !region.Test(north.x, north.y)) {
      stack.emplace_back(north, current.depth + 1);
      region.Set(north.x, north.y, true);
    }

    if (edgeset.IsSet(path::CoordOffset::SouthIndex()) && !region.Test(south.x, south.y)) {
      stack.emplace_back(south, current.depth + 1);
      region.Set(south.x, south.y, true);
    }
  }

  // Shrink region down to minimal memory use.
  region.Fit(start.x, start.y, true);
  if (depth_map) {
    depth_map->ShrinkFit(start.x, start.x, start.y, start.y);
  }
  return entrance_position;
}

}  // namespace zero
