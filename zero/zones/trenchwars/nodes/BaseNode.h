#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/zones/trenchwars/TrenchWars.h>

namespace zero {
namespace tw {

// Find an enemy in the base while traveling.
// This will only find enemies that are within our direct view.
struct FindBaseEnemyNode : public behavior::BehaviorNode {
  FindBaseEnemyNode(const char* output_key) : output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;
    auto self = pm.GetSelf();

    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    float radius = ctx.bot->game->connection.settings.ShipSettings[self->ship].GetRadius();

    // We shouldn't fight enemies below this line because we will be wasting our time.
    constexpr float kBaseStartY = 370;
    // How far on each side of the center we should look for an enemy. We don't want to be running all over the base
    // fighting people on the side.
    constexpr float kSearchX = 40.0f;

    Player* nearest_player = nullptr;
    float best_dist_sq = 1024.0f * 1024.0f;

    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* player = pm.players + i;

      if (player->frequency == self->frequency) continue;
      if (player->ship >= 8) continue;
      if (player->position.y > kBaseStartY) continue;
      if (fabsf(512.0f - player->position.x) > kSearchX) continue;
      if (player->IsRespawning()) continue;
      if (player->position == Vector2f(0, 0)) continue;
      if (!pm.IsSynchronized(*player)) continue;

      float dist_sq = self->position.DistanceSq(player->position);
      if (dist_sq > best_dist_sq) continue;

      CastResult result = ctx.bot->game->GetMap().CastShip(self, radius, player->position);
      if (result.hit) continue;

      nearest_player = player;
      best_dist_sq = dist_sq;
    }

    if (nearest_player == nullptr) return behavior::ExecuteResult::Failure;

    ctx.blackboard.Set<Player*>(output_key, nearest_player);

    return behavior::ExecuteResult::Success;
  }

  const char* output_key = nullptr;
};

// This checks the top area and the vertical shaft for enemies.
// TODO: Might need to check other pub maps to see if it's fine.
// TODO: Could probably generate the rect sizes from the map data.
// Returns Success if no enemies found.
struct EmptyEntranceNode : public behavior::BehaviorNode {
  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;

    auto self = pm.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;
    TrenchWars* tw = *opt_tw;

    Vector2f top_center = tw->flag_position + Vector2f(0.5f, 4.5f);
    Rectangle top_rect(top_center - Vector2f(8.5f, 2.5f), top_center + Vector2f(8.5f, 3.5f));
    Rectangle vertical_rect(top_center - Vector2f(4, 3), top_center + Vector2f(4, 11));

    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* player = pm.players + i;

      if (player->ship >= 8) continue;
      if (player->IsRespawning()) continue;
      if (player->frequency == self->frequency) continue;
      if (player->enter_delay > 0.0f) continue;

      if (top_rect.Contains(player->position) || vertical_rect.Contains(player->position)) {
        return behavior::ExecuteResult::Failure;
      }
    }

    return behavior::ExecuteResult::Success;
  }
};

// Returns success if we are within nearby_threshold tiles of the 'entrance' position.
struct NearEntranceNode : public behavior::BehaviorNode {
  NearEntranceNode(float nearby_threshold) : nearby_threshold(nearby_threshold) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    if (!ctx.bot->bot_controller->pathfinder) return behavior::ExecuteResult::Failure;

    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;

    float radius = ctx.bot->game->connection.settings.ShipSettings[self->ship].GetRadius();
    TrenchWars* tw = *opt_tw;

    path::Path entrance_path = ctx.bot->bot_controller->pathfinder->FindPath(
        ctx.bot->game->GetMap(), self->position, tw->entrance_position, radius, self->frequency);

    if (entrance_path.GetRemainingDistance() < nearby_threshold) {
      return behavior::ExecuteResult::Success;
    }

    return behavior::ExecuteResult::Failure;
  }

  float nearby_threshold;
};

// Goes over the two paths into the base to find which one is less contested.
struct SelectBestEntranceSideNode : public behavior::BehaviorNode {
  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;
    auto self = pm.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    if (!ctx.bot->bot_controller->pathfinder) return behavior::ExecuteResult::Failure;

    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;

    TrenchWars* tw = *opt_tw;

    if (tw->left_entrance_path.Empty() || tw->right_entrance_path.Empty()) return behavior::ExecuteResult::Failure;

    size_t left_index = 0;
    size_t right_index = 0;

    float left_threat = GetPathThreat(ctx, *self, tw->left_entrance_path, &left_index);
    float right_threat = GetPathThreat(ctx, *self, tw->right_entrance_path, &right_index);

    // If we make it this far through a path, continue with it into the flagroom.
    constexpr float kPathTravelPercentBruteForce = 0.25f;

    if (left_index / (float)tw->left_entrance_path.points.size() >= kPathTravelPercentBruteForce) {
      ctx.bot->bot_controller->current_path = tw->left_entrance_path;
      ctx.bot->bot_controller->current_path.index = left_index;
      return behavior::ExecuteResult::Success;
    }

    if (right_index / (float)tw->right_entrance_path.points.size() >= kPathTravelPercentBruteForce) {
      ctx.bot->bot_controller->current_path = tw->right_entrance_path;
      ctx.bot->bot_controller->current_path.index = right_index;
      return behavior::ExecuteResult::Success;
    }

    if (left_threat > self->energy && right_threat > self->energy) {
      return behavior::ExecuteResult::Failure;
    }

    path::Path old_path = ctx.bot->bot_controller->current_path;

    float chosen_threat = left_threat;

    if (left_threat < right_threat) {
      ctx.bot->bot_controller->current_path = tw->left_entrance_path;
      ctx.bot->bot_controller->current_path.index = left_index;
      chosen_threat = left_threat;
    } else {
      ctx.bot->bot_controller->current_path = tw->right_entrance_path;
      ctx.bot->bot_controller->current_path.index = right_index;
      chosen_threat = right_threat;
    }

    // The amount of path points we should look forward to see how many teammates surround us.
    constexpr size_t kForwardPointCount = 3;
    constexpr float kNearbyDistanceSq = 3.0f * 3.0f;

    auto& path = ctx.bot->bot_controller->current_path;

    size_t nearest_point = FindNearestPathPoint(path, self->position);
    size_t forward_point = nearest_point + kForwardPointCount;

    if (forward_point > path.points.size() - 1) {
      forward_point = path.points.size() - 1;
    }

    size_t forward_teammate_count = 0;
    size_t total_teammate_count = 0;

    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* p = pm.players + i;

      if (p->id == self->id) continue;
      if (p->ship >= 8) continue;
      if (p->frequency != self->frequency) continue;
      if (p->enter_delay > 0.0f) continue;

      ++total_teammate_count;
      if (p->position.DistanceSq(path.points[forward_point]) < kNearbyDistanceSq) {
        ++forward_teammate_count;
      }
    }

    if (total_teammate_count > 0 && forward_teammate_count == 0 && chosen_threat > 1.0f) {
      ctx.bot->bot_controller->current_path = old_path;
      return behavior::ExecuteResult::Failure;
    }

    return behavior::ExecuteResult::Success;
  }

  float GetPathThreat(behavior::ExecuteContext& ctx, Player& self, const path::Path& path, size_t* index) {
    size_t start_index = FindNearestPathPoint(path, self.position);
    size_t half_index = path.points.size() / 2;
    size_t end_index = start_index + 15;

    // Don't bother checking the threat inside of the flagroom, only the entranceway.
    if (end_index > half_index) end_index = half_index;

    *index = start_index;

    float total_threat = 0.0f;

    for (size_t i = start_index; i < end_index; ++i) {
      Vector2f point = path.points[i];

      float threat = ctx.bot->bot_controller->influence_map.GetValue(point);
      total_threat += threat;
    }

    return total_threat;
  }

  size_t FindNearestPathPoint(const path::Path& path, Vector2f position) {
    size_t index = 0;
    float last_dist_sq = 1024.0f * 1024.0f;

    // Loop through the nodes until we start getting farther away from the position, then use the last node.
    for (index = 0; index < path.points.size(); ++index) {
      float dist_sq = path.points[index].DistanceSq(position);

      if (index > 0 && dist_sq > last_dist_sq) {
        return index - 1;
      }

      last_dist_sq = dist_sq;
    }

    return 0;
  }
};

struct InFlagroomNode : public behavior::BehaviorNode {
  InFlagroomNode(const char* position_key) : position_key(position_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;

    auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
    if (!opt_position) return behavior::ExecuteResult::Failure;

    bool in_fr = (*opt_tw)->fr_bitset.Test(*opt_position);

    return in_fr ? behavior::ExecuteResult::Success : behavior::ExecuteResult::Failure;
  }

  const char* position_key = nullptr;
};

// Returns success if the target player has some number of teammates within the flag room, including self.
struct FlagroomPresenceNode : public behavior::BehaviorNode {
  FlagroomPresenceNode(u32 count) : count_check(count) {}
  FlagroomPresenceNode(const char* count_key) : count_key(count_key) {}
  FlagroomPresenceNode(const char* count_key, const char* player_key) : count_key(count_key), player_key(player_key) {}
  FlagroomPresenceNode(u32 count, const char* player_key) : count_check(count), player_key(player_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;
    auto player = pm.GetSelf();

    if (player_key) {
      auto opt_player = ctx.blackboard.Value<Player*>(player_key);
      if (!opt_player) return behavior::ExecuteResult::Failure;

      player = *opt_player;
    }

    if (!player) return behavior::ExecuteResult::Failure;

    u32 count = 0;
    u32 count_threshold = count_check;

    if (count_key) {
      auto opt_count_threshold = ctx.blackboard.Value<u32>(count_key);
      if (!opt_count_threshold) return behavior::ExecuteResult::Failure;

      count_threshold = *opt_count_threshold;
    }

    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;

    const auto& bitset = (*opt_tw)->fr_bitset;

    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* check_player = pm.players + i;

      if (check_player->ship >= 8) continue;
      if (check_player->frequency != player->frequency) continue;
      if (check_player->enter_delay > 0.0f) continue;
      if (!bitset.Test(check_player->position)) continue;

      if (++count > count_threshold) {
        return behavior::ExecuteResult::Success;
      }
    }

    return behavior::ExecuteResult::Failure;
  }

  u32 count_check = 0;
  const char* player_key = nullptr;
  const char* count_key = nullptr;
};

// Returns success if our team fully controls the flagroom or it's empty.
struct SafeFlagroomNode : public behavior::BehaviorNode {
  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;
    auto self = pm.GetSelf();

    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;
    TrenchWars* tw = *opt_tw;

    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* player = pm.players + i;

      if (player->ship >= 8) continue;
      if (player->frequency != self->frequency) continue;

      if (tw->fr_bitset.Test(player->position)) {
        return behavior::ExecuteResult::Failure;
      }
    }

    return behavior::ExecuteResult::Success;
  }
};

enum class FlagroomQuadrantRegion { NorthEast, NorthWest, SouthWest, SouthEast };

struct FlagroomQuadrant {
  u16 enemy_count;
  u16 team_count;
};
struct FlagroomPartition {
  FlagroomQuadrant quadrants[4];

  inline FlagroomQuadrant& GetQuadrant(FlagroomQuadrantRegion region) { return quadrants[(size_t)region]; }
  inline const FlagroomQuadrant& GetQuadrant(FlagroomQuadrantRegion region) const { return quadrants[(size_t)region]; }
};

// Determine the flagroom quadrant of a position based on the center.
inline FlagroomQuadrantRegion GetQuadrantRegion(Vector2f center, Vector2f position) {
  Vector2f offset = position - center;
  FlagroomQuadrantRegion region = FlagroomQuadrantRegion::NorthEast;

  if (offset.x >= 0 && offset.y <= 0) {
    region = FlagroomQuadrantRegion::NorthEast;
  } else if (offset.x < 0 && offset.y <= 0) {
    region = FlagroomQuadrantRegion::NorthWest;
  } else if (offset.x < 0 && offset.y > 0) {
    region = FlagroomQuadrantRegion::SouthWest;
  } else if (offset.x >= 0 && offset.y > 0) {
    region = FlagroomQuadrantRegion::SouthEast;
  }

  return region;
}

// Divides up the flagroom into quadrants and stores the data in output_key.
struct FlagroomPartitionNode : public behavior::BehaviorNode {
  FlagroomPartitionNode(const char* output_key) : output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;

    auto self = pm.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;

    TrenchWars* tw = *opt_tw;

    FlagroomPartition partition = {};

    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* player = pm.players + i;

      if (player->ship >= 8) continue;
      if (player->IsRespawning()) continue;
      if (!pm.IsSynchronized(*player)) continue;
      if (!tw->fr_bitset.Test(player->position)) continue;

      FlagroomQuadrantRegion region = GetQuadrantRegion(tw->flag_position, player->position);
      FlagroomQuadrant& quad = partition.GetQuadrant(region);

      if (player->frequency == self->frequency) {
        ++quad.team_count;
      } else {
        ++quad.enemy_count;
      }
    }

    ctx.blackboard.Set(output_key, partition);

    return behavior::ExecuteResult::Success;
  }

  const char* output_key = nullptr;
};

}  // namespace tw
}  // namespace zero
