#include <zero/game/Game.h>
#include <zero/path/NodeProcessor.h>
#include <zero/path/Pathfinder.h>
//
#include <math.h>
#include <xmmintrin.h>

#include <thread>

namespace zero {
namespace path {

// The edge weight for entering a safe zone from a non-safe tile.
// This is set here instead of stored in a node so traveling through multiple safe tiles isn't very expensive.
constexpr float kSafetyWeight = 300.0f;

static inline float fast_sqrt(float v) {
  __m128 v_x4 = _mm_set1_ps(v);

  __m128 result = _mm_sqrt_ps(v_x4);

  _mm_store_ss(&v, result);

  return v;
}

static inline NodePoint ToNodePoint(const Vector2f v) {
  NodePoint np;

  np.x = (uint16_t)v.x;
  np.y = (uint16_t)v.y;

  return np;
}

static inline float Euclidean(NodeProcessor& processor, const Node* from, const Node* to) {
  NodePoint from_p = processor.GetPoint(from);
  NodePoint to_p = processor.GetPoint(to);

  float dx = static_cast<float>(from_p.x - to_p.x);
  float dy = static_cast<float>(from_p.y - to_p.y);

  return sqrt(dx * dx + dy * dy);
}

static inline float Euclidean(const NodePoint& __restrict from_p, const NodePoint& __restrict to_p) {
  float dx = static_cast<float>(from_p.x - to_p.x);
  float dy = static_cast<float>(from_p.y - to_p.y);

  __m128 mult = _mm_set_ss(dx * dx + dy * dy);
  __m128 result = _mm_sqrt_ss(mult);

  return _mm_cvtss_f32(result);
}

Pathfinder::Pathfinder(std::unique_ptr<NodeProcessor> processor, RegionRegistry& regions)
    : processor_(std::move(processor)), regions_(regions) {}

Path Pathfinder::FindPath(const Map& map, const Vector2f& from, const Vector2f& to, float radius, u16 frequency) {
  Path path = {};
  Node* start = processor_->GetNode(ToNodePoint(from));
  Node* goal = processor_->GetNode(ToNodePoint(to));

  if (start == nullptr || goal == nullptr) {
    return path;
  }

  if (!(start->flags & NodeFlag_Traversable)) return path;
  if (!(goal->flags & NodeFlag_Traversable)) return path;

  NodePoint start_p = processor_->GetPoint(start);
  NodePoint goal_p = processor_->GetPoint(goal);

  if (!regions_.IsConnected(MapCoord(start_p.x, start_p.y), MapCoord(goal_p.x, goal_p.y))) {
    return path;
  }

  // clear vector then add start node
  openset_.Clear();
  openset_.Push(start);

  // at the start there is only one node here, the start node
  while (!openset_.Empty()) {
    // grab front item then delete it
    Node* node = openset_.Pop();

    // this is the only way to break the pathfinder
    if (node == goal) {
      break;
    }

    node->flags &= ~NodeFlag_Openset;

    NodePoint node_point = processor_->GetPoint(node);

    // Returns neighbor nodes that are not solid.
    EdgeSet edges = processor_->FindEdges(node, radius);

    if (edges.dynamic != 0) {
      // If we considered any possible dynamic tiles then consider the path to be dynamic.
      // This will cause it to be cleared and re-evaluated on door update.
      path.dynamic = true;
    }

    for (size_t i = 0; i < 8; ++i) {
      if (!edges.IsSet(i)) continue;

      CoordOffset offset = CoordOffset::FromIndex(i);

      NodePoint edge_point(node_point.x + offset.x, node_point.y + offset.y);
      Node* edge = processor_->GetNode(edge_point);

      // This edge has a dynamic brick, so we need to check the state
      if (edge->flags & NodeFlag_Brick) {
        path.dynamic = true;

        if (map.IsSolid(edge_point.x, edge_point.y, frequency)) {
          continue;
        }
      }

      touched_.push_back(edge);

      // The cost to this neighbor is the cost to the current node plus the edge weight times the distance between the
      // nodes.
      // Euclidean could be calculated based on edge index if all 8 are considered again.
      float cost = node->g + edge->GetWeight();

      // Only set a very high movement cost upon first entering a safety tile.
      if ((edge->flags & NodeFlag_Safety) && !(node->flags & NodeFlag_Safety)) {
        cost = node->g + kSafetyWeight;
      }

      // Compute a heuristic from this neighbor to the end goal.
      float h = Euclidean(edge_point, goal_p);

      // The path to this node is lower than it was previously, so update its values.
      if (cost < edge->g || !(edge->flags & NodeFlag_Touched)) {
        edge->g = cost;
        edge->f = edge->g + h;

        edge->parent_id = processor_->GetNodeIndex(node);

        if (!(edge->flags & NodeFlag_Touched)) {
          touched_.push_back(edge);
          edge->flags |= NodeFlag_Touched;
        }

        if (!(edge->flags & NodeFlag_Openset)) {
          // The node is not in the openset so add it.
          edge->flags |= NodeFlag_Openset;
          openset_.Push(edge);
        }
      }
    }
  }

  if (goal->parent_id != ~0) {
    path.Add(Vector2f(start_p.x + 0.5f, start_p.y + 0.5f));
  }

  // Construct path backwards from goal node
  std::vector<NodePoint> points;
  Node* current = goal;

  while (current != nullptr && current != start) {
    NodePoint p = processor_->GetPoint(current);
    points.push_back(p);
    current = processor_->GetNodeFromIndex(current->parent_id);
  }

  // Reverse and store as vector
  for (std::size_t i = 0; i < points.size(); ++i) {
    std::size_t index = points.size() - i - 1;
    Vector2f pos(points[index].x + 0.5f, points[index].y + 0.5f);

    pos = map.ResolveShipCollision(pos, radius, 0xFFFF);

    path.Add(pos);
  }

  for (Node* node : touched_) {
    node->flags &= ~NodeFlag_Initialized;
  }
  touched_.clear();

  return path;
}

// This is pretty expensive to happen on a ship change.
// TODO: It could be faster by using simd solid checks.
inline static float GetWallDistance(const Map& map, u16 x, u16 y, u16 radius) {
  float closest_sq = std::numeric_limits<float>::max();

  for (s16 offset_y = -radius; offset_y <= radius; ++offset_y) {
    for (s16 offset_x = -radius; offset_x <= radius; ++offset_x) {
      u16 check_x = x + offset_x;
      u16 check_y = y + offset_y;

      if (map.IsSolidEmptyDoors(check_x, check_y, 0xFFFF)) {
        float dist_sq = (float)(offset_x * offset_x + offset_y * offset_y);

        if (dist_sq < closest_sq) {
          closest_sq = dist_sq;
        }
      }
    }
  }

  return sqrt(closest_sq);
}

static void CalculateTraversables(const Map& map, NodeProcessor& processor, float ship_radius, s16 x_start, s16 y_start,
                                  s16 x_end, s16 y_end, OccupiedRect* scratch_rects) {
  u32 frequency = 0xFFFF;

  for (u16 y = y_start; y < y_end; ++y) {
    for (u16 x = x_start; x < x_end; ++x) {
      if (map.IsSolidEmptyDoors(x, y, 0xFFFF)) continue;

      Node* node = processor.GetNode(NodePoint(x, y));

      if (map.CanOverlapTile(Vector2f(x, y), ship_radius, frequency)) {
        node->flags |= NodeFlag_Traversable;

        size_t rect_count = map.GetAllOccupiedRects(Vector2f(x, y), ship_radius, frequency, scratch_rects);

        // This might be a diagonal tile
        if (rect_count == 2) {
          // Check if the two occupied rects are offset on both axes.
          if (scratch_rects[0].start_x != scratch_rects[1].start_x &&
              scratch_rects[0].start_y != scratch_rects[1].start_y) {
            // This is a diagonal-only tile, so skip it.
            node->flags &= ~NodeFlag_Traversable;
          }
        }
      }
    }
  }
}

static void CalculateEdges(const Map& map, NodeProcessor& processor, float ship_radius, Pathfinder::WeightConfig config,
                           s16 x_start, s16 y_start, s16 x_end, s16 y_end) {
  u32 frequency = 0xFFFF;

  OccupiedRect* occupied_scratch = (OccupiedRect*)malloc(sizeof(OccupiedRect) * 2048);

  for (u16 y = y_start; y < y_end; ++y) {
    for (u16 x = x_start; x < x_end; ++x) {
      if (map.IsSolidEmptyDoors(x, y, frequency)) continue;

      Node* node = processor.GetNode(NodePoint(x, y));
      NodePoint current_point = processor.GetPoint(node);
      EdgeSet edges = processor.CalculateEdges(node, ship_radius, occupied_scratch);

      node->SetWeight(1.0f);

      processor.SetEdgeSet(x, y, edges);

      if (config.weight_type != Pathfinder::WeightType::Flat) {
        int close_distance = config.wall_distance;
        float distance = GetWallDistance(map, x, y, close_distance);

        if (distance < 1) distance = 1;

        if (distance < close_distance) {
          if (config.weight_type == Pathfinder::WeightType::Linear) {
            node->SetWeight(close_distance / distance);
          } else {
            float inv_dist = close_distance - distance;
            node->SetWeight(inv_dist * inv_dist);
          }
        }
      }

      TileId tile_id = map.GetTileId(current_point.x, current_point.y);

      if (tile_id == kTileIdSafe) {
        node->flags |= NodeFlag_Safety;
      }
    }
  }

  free(occupied_scratch);
}

void Pathfinder::CreateMapWeights(MemoryArena& temp_arena, const Map& map, WeightConfig config) {
  float ship_radius = config.ship_radius;
  u32 frequency = config.frequency;

  MemoryRevert reverter = temp_arena.GetReverter();
  OccupiedRect* scratch_rects = memory_arena_push_type_count(&temp_arena, OccupiedRect, 256);

  this->config = config;

  constexpr size_t kThreadCount = 12;
  std::thread threads[kThreadCount];

  // First loop over tiles and calculate all of the traversables.
  for (size_t i = 0; i < kThreadCount; ++i) {
    s16 per_thread = 1024 / (s16)kThreadCount;

    s16 x_start = (s16)i * per_thread;
    s16 y_start = 0;
    s16 x_end = ((s16)i + 1) * per_thread;
    s16 y_end = 1024;

    if (i == kThreadCount - 1) {
      s16 remainder = (s16)kThreadCount % 1024;
      x_end += remainder;
    }

    threads[i] = std::thread(CalculateTraversables, map, std::ref(*processor_), ship_radius, x_start, y_start, x_end,
                             y_end, scratch_rects + i * (256 / kThreadCount));
  }

  // We must wait for all of the traversables to be calculated before we start calculating edges.
  for (size_t i = 0; i < kThreadCount; ++i) {
    threads[i].join();
  }

  // Loop over tiles to calculate the node edges.
  for (size_t i = 0; i < kThreadCount; ++i) {
    s16 per_thread = 1024 / (s16)kThreadCount;

    s16 x_start = (s16)i * per_thread;
    s16 y_start = 0;
    s16 x_end = ((s16)i + 1) * per_thread;
    s16 y_end = 1024;

    if (i == kThreadCount - 1) {
      s16 remainder = (s16)kThreadCount % 1024;
      x_end += remainder;
    }

    threads[i] =
        std::thread(CalculateEdges, map, std::ref(*processor_), ship_radius, config, x_start, y_start, x_end, y_end);
  }

  for (size_t i = 0; i < kThreadCount; ++i) {
    threads[i].join();
  }
}

}  // namespace path
}  // namespace zero
