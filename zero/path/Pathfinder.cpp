#include <xmmintrin.h>
#include <zero/game/Game.h>
#include <zero/path/NodeProcessor.h>
#include <zero/path/Pathfinder.h>

namespace zero {
namespace path {

inline float fast_sqrt(float v) {
  __m128 v_x4 = _mm_set1_ps(v);

  __m128 result = _mm_sqrt_ps(v_x4);

  _mm_store_ss(&v, result);

  return v;
}

inline NodePoint ToNodePoint(const Vector2f v) {
  NodePoint np;

  np.x = (uint16_t)v.x;
  np.y = (uint16_t)v.y;

  return np;
}

inline float Euclidean(NodeProcessor& processor, const Node* from, const Node* to) {
  NodePoint from_p = processor.GetPoint(from);
  NodePoint to_p = processor.GetPoint(to);

  float dx = static_cast<float>(from_p.x - to_p.x);
  float dy = static_cast<float>(from_p.y - to_p.y);

  return sqrt(dx * dx + dy * dy);
}

inline float Euclidean(const NodePoint& __restrict from_p, const NodePoint& __restrict to_p) {
  float dx = static_cast<float>(from_p.x - to_p.x);
  float dy = static_cast<float>(from_p.y - to_p.y);

  __m128 mult = _mm_set_ss(dx * dx + dy * dy);
  __m128 result = _mm_sqrt_ss(mult);

  return _mm_cvtss_f32(result);
}

Pathfinder::Pathfinder(std::unique_ptr<NodeProcessor> processor, RegionRegistry& regions)
    : processor_(std::move(processor)), regions_(regions) {}

std::vector<Vector2f> Pathfinder::FindPath(const Map& map, const Vector2f& from, const Vector2f& to, float radius) {
  std::vector<Vector2f> path;

  Node* start = processor_->GetNode(ToNodePoint(from));
  Node* goal = processor_->GetNode(ToNodePoint(to));

  if (start == nullptr || goal == nullptr) {
    return path;
  }

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

    touched_.push_back(node);

    // this is the only way to break the pathfinder
    if (node == goal) {
      break;
    }

    node->flags |= NodeFlag_Closed;

    NodePoint node_point = processor_->GetPoint(node);

    // returns neighbor nodes that are not solid
    NodeConnections connections = processor_->FindEdges(node, start, goal, radius);

    for (std::size_t i = 0; i < connections.count; ++i) {
      Node* edge = connections.neighbors[i];
      NodePoint edge_point = processor_->GetPoint(edge);

      touched_.push_back(edge);

      float cost = node->g + edge->weight * Euclidean(node_point, edge_point);

      if ((edge->flags & NodeFlag_Closed) && cost < edge->g) {
        edge->flags &= ~NodeFlag_Closed;
      }

      float h = Euclidean(edge_point, goal_p);

      if (!(edge->flags & NodeFlag_Openset) || cost + h < edge->f) {
        edge->parent = node;
        edge->flags |= NodeFlag_Openset;
        edge->g = cost;
        edge->f = edge->g + h;

        openset_.Push(edge);
      }
    }
  }

  if (goal->parent) {
    path.push_back(Vector2f(start_p.x + 0.5f, start_p.y + 0.5f));
  }

  // Construct path backwards from goal node
  std::vector<NodePoint> points;
  Node* current = goal;

  while (current != nullptr && current != start) {
    NodePoint p = processor_->GetPoint(current);
    points.push_back(p);
    current = current->parent;
  }

  // Reverse and store as vector
  for (std::size_t i = 0; i < points.size(); ++i) {
    std::size_t index = points.size() - i - 1;
    Vector2f pos(points[index].x + 0.5f, points[index].y + 0.5f);

    path.push_back(pos);
  }

  for (Node* node : touched_) {
    node->flags &= ~NodeFlag_Initialized;
  }
  touched_.clear();

  return path;
}

inline Vector2f ClosestWall(const Map& map, Vector2f pos, int search) {
  float closest_dist = std::numeric_limits<float>::max();
  Vector2f closest;

  Vector2f base(std::floor(pos.x), std::floor(pos.y));
  for (int y = -search; y <= search; ++y) {
    for (int x = -search; x <= search; ++x) {
      Vector2f current = base + Vector2f((float)x, (float)y);

      if (!map.IsSolid((unsigned short)current.x, (unsigned short)current.y, 0xFFFF)) {
        continue;
      }

      float dist = BoxPointDistance(current, Vector2f(1, 1), pos);

      if (dist < closest_dist) {
        closest_dist = dist;
        closest = current;
      }
    }
  }

  return closest;
}

bool IsPassablePath(const Map& map, Vector2f from, Vector2f to, float radius, u32 frequency) {
  const Vector2f direction = Normalize(to - from);
  const Vector2f side = Perpendicular(direction) * radius;
  const float distance = from.Distance(to);

  CastResult cast_center = map.Cast(from, direction, distance, frequency);
  CastResult cast_side1 = map.Cast(from + side, direction, distance, frequency);
  CastResult cast_side2 = map.Cast(from - side, direction, distance, frequency);

  return !cast_center.hit && !cast_side1.hit && !cast_side2.hit;
}

std::vector<Vector2f> Pathfinder::SmoothPath(Game& game, const std::vector<Vector2f>& path, float ship_radius) {
  return path;

  std::vector<Vector2f> result;

  // How far away it should try to push the path from walls
  float push_distance = ship_radius * 1.5f;

  result.resize(path.size());

  if (!path.empty()) {
    result[0] = path[0];
  }

  for (std::size_t i = 1; i < path.size(); ++i) {
    Vector2f current = path[i];
    Vector2f closest = ClosestWall(game.GetMap(), current, (int)ceilf(push_distance + 1));
    Vector2f new_pos = current;

    if (closest != Vector2f(0, 0)) {
      // Attempt to push the path outward from the wall
      // TODO: iterative box penetration push

      Vector2f center = closest + Vector2f(0.5, 0.5);
      Vector2f direction = Normalize(center - current);
      CastResult cast_result = game.GetMap().Cast(current, direction, push_distance, 0xFFFF);

      if (cast_result.hit) {
        Vector2f hit = cast_result.position;
        float dist = hit.Distance(current);
        float force = push_distance - dist;

        new_pos = current + Normalize(current - hit) * force;
      }
    }

    if (current != new_pos) {
      // Make sure the new node is in line of sight
      if (!IsPassablePath(game.GetMap(), current, new_pos, ship_radius, 0xFFFF)) {
        new_pos = current;
      }
    }

    result[i] = new_pos;
  }

#if 1  // Don't cull the path if this is enabled
  return result;
#endif

  if (result.size() <= 2) return result;

  std::vector<Vector2f> minimum;
  minimum.reserve(result.size());

  Vector2f prev = result[0];
  for (std::size_t i = 1; i < result.size(); ++i) {
    Vector2f curr = result[i];
    Vector2f direction = Normalize(curr - prev);
    Vector2f side = Perpendicular(direction) * ship_radius;
    float dist = prev.Distance(curr);

    CastResult cast_center = game.GetMap().Cast(prev, direction, dist, 0xFFFF);
    CastResult cast_side1 = game.GetMap().Cast(prev + side, direction, dist, 0xFFFF);
    CastResult cast_side2 = game.GetMap().Cast(prev - side, direction, dist, 0xFFFF);

    if (cast_center.hit || cast_side1.hit || cast_side2.hit) {
      if (minimum.size() > result.size()) {
        minimum = result;
        break;
      }

      if (!minimum.empty() && result[i - 1] != minimum.back()) {
        minimum.push_back(result[i - 1]);
        prev = minimum.back();
        i--;
      } else {
        minimum.push_back(result[i]);
        prev = minimum.back();
      }
    }
  }

  minimum.push_back(result.back());

  result = minimum;
  return result;
}

std::vector<Vector2f> Pathfinder::CreatePath(Game& game, Vector2f from, Vector2f to, float radius) {
  bool build = true;

  if (build) {
    path_.clear();
#if 0
    for (Weapon* weapon : processor_->GetGame().GetWeapons()) {
      const Player* weapon_player = processor_->GetGame().GetPlayerById(weapon->GetPlayerId());
      if (weapon_player == nullptr) continue;
      if (weapon_player->frequency == processor_->GetGame().GetPlayer().frequency) continue;
      if (weapon->IsMine()) mines.push_back(weapon->GetPosition());
    }
#endif
    path_ = FindPath(processor_->GetGame().connection.map, from, to, radius);
  }

  return path_;
}

float Pathfinder::GetWallDistance(const Map& map, u16 x, u16 y, u16 radius) {
  float closest_sq = std::numeric_limits<float>::max();

  for (s16 offset_y = -radius; offset_y <= radius; ++offset_y) {
    for (s16 offset_x = -radius; offset_x <= radius; ++offset_x) {
      u16 check_x = x + offset_x;
      u16 check_y = y + offset_y;

      if (map.IsSolid(check_x, check_y, 0xFFFF)) {
        float dist_sq = (float)(offset_x * offset_x + offset_y * offset_y);

        if (dist_sq < closest_sq) {
          closest_sq = dist_sq;
        }
      }
    }
  }
  return sqrt(closest_sq);
}

void Pathfinder::CreateMapWeights(const Map& map, float ship_radius) {
  for (u16 y = 0; y < 1024; ++y) {
    for (u16 x = 0; x < 1024; ++x) {
      Node* node = this->processor_->GetNode(NodePoint(x, y));

      if (map.CanOccupy(Vector2f(x, y), ship_radius, 0xFFFF)) {
        node->flags |= NodeFlag_Traversable;
      }

      if (map.IsSolid(x, y, 0xFFFF)) continue;

      // Search width is double this number (for 8, searches a 16 x 16 square).
      int close_distance = 5;

      /* Causes exponentianl weight increase as the path gets closer to wall tiles.
      Known Issue: 3 tile gaps and 4 tile gaps will carry the same weight since each tiles closest wall is 2 tiles
      away.*/

      float distance = GetWallDistance(map, x, y, close_distance);

      // Nodes are initialized with a weight of 1.0f, so never calculate when the distance is greater or equal
      // because the result will be less than 1.0f.
      if (distance < close_distance) {
        float weight = close_distance / distance;
        // paths directly next to a wall will be a last resort, 1 tile from wall very unlikely
        node->weight = powf(weight, 4.0f);
      }
    }
  }
}

}  // namespace path
}  // namespace zero
