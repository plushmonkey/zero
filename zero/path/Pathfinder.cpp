#include <zero/game/Game.h>
#include <zero/path/NodeProcessor.h>
#include <zero/path/Pathfinder.h>

namespace zero {
namespace path {

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

Pathfinder::Pathfinder(std::unique_ptr<NodeProcessor> processor, RegionRegistry& regions)
    : processor_(std::move(processor)), regions_(regions) {}

std::vector<Vector2f> Pathfinder::FindPath(const Map& map, const Vector2f& from, const Vector2f& to, float radius) {
  std::vector<Vector2f> path;

  // Clear the touched nodes before pathfinding.
  for (Node* node : touched_nodes_) {
    // Setting the flag to zero causes GetNode to reset the node on next fetch.
    node->flags = 0;
  }
  touched_nodes_.clear();

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

  touched_nodes_.insert(start);
  touched_nodes_.insert(goal);

  // at the start there is only one node here, the start node
  while (!openset_.Empty()) {
    // grab front item then delete it
    Node* node = openset_.Pop();

    touched_nodes_.insert(node);

    // this is the only way to break the pathfinder
    if (node == goal) {
      break;
    }

    node->flags |= NodeFlag_Closed;

    // returns neighbor nodes that are not solid
    NodeConnections connections = processor_->FindEdges(node, start, goal);

    for (std::size_t i = 0; i < connections.count; ++i) {
      Node* edge = connections.neighbors[i];

      touched_nodes_.insert(edge);

      float cost = node->g + edge->weight * Euclidean(*processor_, node, edge);

      if ((edge->flags & NodeFlag_Closed) && cost < edge->g) {
        edge->flags &= ~NodeFlag_Closed;
      }

      float h = Euclidean(*processor_, edge, goal);

      if (!(edge->flags & NodeFlag_Openset) || cost + h < edge->f) {
        edge->g = cost;
        edge->f = edge->g + h;
        edge->parent = node;

        edge->flags |= NodeFlag_Openset;

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

  return path;
}

std::vector<Vector2f> Pathfinder::SmoothPath(Game& game, const std::vector<Vector2f>& path, float ship_radius) {
  std::vector<Vector2f> result = path;
  return result;

#if 0
  for (std::size_t i = 0; i < result.size(); i++) {
    std::size_t next = i + 1;

    if (next == result.size() - 1) {
      return result;
    }

    bool hit = DiameterRayCastHit(bot, result[i], result[next], 3.0f);

    if (!hit) {
      result.erase(result.begin() + next);
      i--;
    }
  }

  return result;
#endif
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

void Pathfinder::CreateMapWeights(const Map& map) {
  for (u16 y = 0; y < 1024; ++y) {
    for (u16 x = 0; x < 1024; ++x) {
      if (map.IsSolid(x, y, 0xFFFF)) continue;

      Node* node = this->processor_->GetNode(NodePoint(x, y));

      // Search width is double this number (for 8, searches a 16 x 16 square).
      int close_distance = 8;

      /* Causes exponentianl weight increase as the path gets closer to wall tiles.
      Known Issue: 3 tile gaps and 4 tile gaps will carry the same weight since each tiles closest wall is 2 tiles
      away.*/

      float distance = GetWallDistance(map, x, y, close_distance);

      // Nodes are initialized with a weight of 1.0f, so never calculate when the distance is greater or equal
      // because the result will be less than 1.0f.
      if (distance < close_distance) {
        float weight = 8.0f / distance;
        // paths directly next to a wall will be a last resort, 1 tile from wall very unlikely
        node->weight = (float)std::pow(weight, 4.0);
      }
    }
  }
}

}  // namespace path
}  // namespace zero
