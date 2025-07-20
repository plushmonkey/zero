#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/zones/trenchwars/TrenchWars.h>

namespace zero {
namespace tw {

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

// Looks for enemies in the pocket areas next to the entrance.
// Returns Failure if there is an enemy in there.
struct EmptySideAreaNode : public behavior::BehaviorNode {
  enum class Side { West, East };
  EmptySideAreaNode(Side side) : side(side) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;

    auto self = pm.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;
    TrenchWars* tw = *opt_tw;

    Rectangle west_rectangle(Vector2f(500, 283), Vector2f(504, 293));
    Rectangle east_rectangle(Vector2f(520, 283), Vector2f(524, 293));

    Rectangle* check_rect = side == Side::West ? &west_rectangle : &east_rectangle;

    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* player = pm.players + i;

      if (player->ship >= 8) continue;
      if (player->frequency == self->frequency) continue;
      if (player->enter_delay > 0.0f) continue;

      if (check_rect->Contains(player->position)) {
        return behavior::ExecuteResult::Failure;
      }
    }

    return behavior::ExecuteResult::Success;
  }

  Side side = Side::West;
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
  SelectBestEntranceSideNode(const char* partition_key) : partition_key(partition_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;
    auto self = pm.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    if (!ctx.bot->bot_controller->pathfinder) return behavior::ExecuteResult::Failure;

    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;

    TrenchWars* tw = *opt_tw;

    auto opt_partition = ctx.blackboard.Value<FlagroomPartition>(partition_key);
    if (!opt_partition) return behavior::ExecuteResult::Failure;

    FlagroomPartition partition = *opt_partition;

    const FlagroomQuadrant& sw_quadrant = partition.GetQuadrant(FlagroomQuadrantRegion::SouthWest);
    const FlagroomQuadrant& se_quadrant = partition.GetQuadrant(FlagroomQuadrantRegion::SouthEast);

    s32 sw_diff = sw_quadrant.team_count - sw_quadrant.enemy_count;
    s32 se_diff = se_quadrant.team_count - se_quadrant.enemy_count;

    if (sw_diff <= 0 && se_diff <= 0) {
      return behavior::ExecuteResult::Failure;
    }

    path::Path* new_path = &tw->left_entrance_path;

    if (se_diff > sw_diff) {
      new_path = &tw->right_entrance_path;
    }

    size_t path_index = FindNearestPathPoint(*new_path, self->position);

    ctx.bot->bot_controller->current_path = *new_path;
    ctx.bot->bot_controller->current_path.index = path_index;

    return behavior::ExecuteResult::Success;
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

  const char* partition_key = nullptr;
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
