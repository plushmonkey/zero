#pragma once

#include <time.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/zones/extremegames/ExtremeGames.h>

namespace zero {
namespace eg {

using namespace behavior;

struct InBaseNode : public BehaviorNode {
  InBaseNode(const char* position_key) : position_key(position_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
    if (!opt_eg) return ExecuteResult::Failure;

    ExtremeGames* eg = *opt_eg;

    auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
    if (!opt_position) return ExecuteResult::Failure;

    Vector2f position = *opt_position;

    if (eg->GetBaseFromPosition(position) == -1) {
      return ExecuteResult::Failure;
    }

    return ExecuteResult::Success;
  }

  const char* position_key = nullptr;
};

struct InFlagroomNode : public BehaviorNode {
  InFlagroomNode(const char* position_key) : position_key(position_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
    if (!opt_eg) return ExecuteResult::Failure;

    ExtremeGames* eg = *opt_eg;

    auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
    if (!opt_position) return ExecuteResult::Failure;

    Vector2f position = *opt_position;

    size_t base_index = eg->GetBaseFromPosition(position);
    if (base_index == -1) return ExecuteResult::Failure;

    bool in_fr = eg->bases[base_index].flagroom_bitset.Test((u16)position.x, (u16)position.y);

    return in_fr ? ExecuteResult::Success : ExecuteResult::Failure;
  }

  const char* position_key = nullptr;
};

struct SameBaseNode : public BehaviorNode {
  SameBaseNode(const char* position_a_key, const char* position_b_key)
      : position_a_key(position_a_key), position_b_key(position_b_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
    if (!opt_eg) return ExecuteResult::Failure;

    ExtremeGames* eg = *opt_eg;

    auto opt_position_a = ctx.blackboard.Value<Vector2f>(position_a_key);
    if (!opt_position_a) return ExecuteResult::Failure;

    Vector2f position_a = *opt_position_a;

    auto opt_position_b = ctx.blackboard.Value<Vector2f>(position_b_key);
    if (!opt_position_b) return ExecuteResult::Failure;

    Vector2f position_b = *opt_position_b;

    size_t a_index = eg->GetBaseFromPosition(position_a);
    size_t b_index = eg->GetBaseFromPosition(position_b);

    if (a_index == -1 || b_index == -1) return ExecuteResult::Failure;
    if (a_index != b_index) return ExecuteResult::Failure;

    return ExecuteResult::Success;
  }

  const char* position_a_key = nullptr;
  const char* position_b_key = nullptr;
};

struct BaseFlagCount {
  u8 team_dropped_flags = 0;
  u8 team_carried_flags = 0;

  u8 enemy_dropped_flags = 0;
  u8 enemy_carried_flags = 0;

  u8 unclaimed = 0;
};

// Computes the flag counts for each base and stores them as vector<BaseFlagCount> for each base that exists.
struct BaseFlagCountQueryNode : public BehaviorNode {
  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return ExecuteResult::Failure;

    auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
    if (!opt_eg) return ExecuteResult::Failure;

    ExtremeGames* eg = *opt_eg;
    auto& game = *ctx.bot->game;

    std::vector<BaseFlagCount> flag_counts(eg->bases.size());

    for (size_t i = 0; i < game.flag_count; ++i) {
      GameFlag* flag = game.flags + i;

      if (flag->flags & GameFlag_Dropped) {
        size_t base_index = eg->GetBaseFromPosition(flag->position);

        if (base_index != -1 && base_index < flag_counts.size()) {
          if (flag->owner == self->frequency) {
            ++flag_counts[base_index].team_dropped_flags;
          } else if (flag->owner != 0xFFFF) {
            ++flag_counts[base_index].enemy_dropped_flags;
          } else {
            ++flag_counts[base_index].unclaimed;
          }
        }
      }
    }

    for (size_t i = 0; i < game.player_manager.player_count; ++i) {
      Player* player = game.player_manager.players + i;

      if (player->ship >= 8) continue;
      if (player->IsRespawning()) continue;

      size_t base_index = eg->GetBaseFromPosition(player->position);

      if (base_index != -1 && base_index < flag_counts.size()) {
        if (player->frequency == self->frequency) {
          flag_counts[base_index].team_carried_flags;
        } else {
          flag_counts[base_index].enemy_carried_flags;
        }
      }
    }

    ctx.blackboard.Set(BaseFlagCountQueryNode::Key(), flag_counts);

    return ExecuteResult::Success;
  }

  static const char* Key() { return "eg_base_flag_counts"; }
};

// This determines which frequency is in control of the base by determining closest to flagroom.
// output_key will be stored as u16 frequency.
struct BaseTeamControlQueryNode : public BehaviorNode {
  BaseTeamControlQueryNode(const char* position_key, const char* output_key)
      : position_key(position_key), output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
    if (!opt_eg) return ExecuteResult::Failure;

    ExtremeGames* eg = *opt_eg;

    auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
    if (!opt_position) return ExecuteResult::Failure;

    Vector2f position = *opt_position;

    size_t base_index = eg->GetBaseFromPosition(position);
    if (base_index == -1) return ExecuteResult::Failure;

    size_t best_path_index = 0;
    Player* best_player = nullptr;

    auto& pm = ctx.bot->game->player_manager;
    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* player = pm.players + i;

      if (player->ship >= 8) continue;
      if (player->IsRespawning()) continue;
      if (eg->GetBaseFromPosition(player->position) != base_index) continue;

      // This can be pretty expensive, might want to profile it to make sure it's fine. Should be.
      size_t path_index = GetClosestPathIndex(eg->bases[base_index].path, player->position);

      if (path_index > best_path_index) {
        best_path_index = path_index;
        best_player = player;
      }
    }

    if (!best_player) return ExecuteResult::Failure;

    ctx.blackboard.Set<u16>(output_key, best_player->frequency);

    return ExecuteResult::Success;
  }

  size_t GetClosestPathIndex(path::Path& path, Vector2f position) {
    size_t best_index = 0;
    float best_dist_sq = 1024.0f * 1024.0f;

    for (size_t i = 0; i < path.points.size(); ++i) {
      float dist_sq = path.points[i].DistanceSq(position);
      if (dist_sq < best_dist_sq) {
        best_index = i;
        best_dist_sq = dist_sq;
      }
    }

    return best_index;
  }

  const char* position_key = nullptr;
  const char* output_key = nullptr;
};

// Returns a position in the flagroom of the base that contains the provided position
struct BaseFlagroomPositionNode : public BehaviorNode {
  BaseFlagroomPositionNode(const char* position_key, const char* output_key)
      : position_key(position_key), output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
    if (!opt_eg) return ExecuteResult::Failure;

    ExtremeGames* eg = *opt_eg;

    auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
    if (!opt_position) return ExecuteResult::Failure;

    Vector2f position = *opt_position;

    size_t base_index = eg->GetBaseFromPosition(position);
    if (base_index == -1) return ExecuteResult::Failure;

    ctx.blackboard.Set(output_key, eg->bases[base_index].flagroom_position);

    return ExecuteResult::Success;
  }

  const char* position_key = nullptr;
  const char* output_key = nullptr;
};

// Go through each base and determine which one we should path to.
// This will prioritize bases with existing flags.
struct FindBestBaseEntranceNode : public BehaviorNode {
  FindBestBaseEntranceNode(const char* output_key) : output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;
    auto self = pm.GetSelf();

    if (!self || self->ship >= 8) return ExecuteResult::Failure;

    auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
    if (!opt_eg) return ExecuteResult::Failure;

    ExtremeGames* eg = *opt_eg;

    std::vector<size_t> flag_counts(eg->bases.size());

    auto& game = *ctx.bot->game;
    for (size_t i = 0; i < game.flag_count; ++i) {
      GameFlag* game_flag = game.flags + i;

      if (game_flag->flags & GameFlag_Dropped) {
        size_t base_index = eg->GetBaseFromPosition(game_flag->position);
        if (base_index != -1) {
          ++flag_counts[base_index];
        }
      }
    }

    for (size_t i = 0; i < game.player_manager.player_count; ++i) {
      auto player = game.player_manager.players + i;

      if (player->flags > 0) {
        size_t base_index = eg->GetBaseFromPosition(player->position);
        if (base_index != -1) {
          flag_counts[base_index] += player->flags;
        }
      }
    }

    size_t best_index = GetCurrentDefaultBase(flag_counts.size());
    size_t best_count = 0;

    for (size_t i = 0; i < flag_counts.size(); ++i) {
      if (flag_counts[i] > best_count) {
        best_index = i;
        best_count = flag_counts[i];
      }
    }

    ctx.blackboard.Set(output_key, eg->bases[best_index].entrance_position);

    return ExecuteResult::Success;
  }

  // Find a base when flag counts are zero.
  // Should be something everyone calculates exactly the same so there is a consensus.
  size_t GetCurrentDefaultBase(size_t base_count) {
    time_t t = time(nullptr);
    tm* gm_time = gmtime(&t);

    size_t hour = gm_time->tm_hour;
    size_t portion = gm_time->tm_min / 20;

    // Cause default base to shift every 20 minutes semi-randomly.
    return (hour * 67217 + portion * 12347) % base_count;
  }

  const char* output_key = nullptr;
};

struct FindBestBaseTeammateNode : public BehaviorNode {
  FindBestBaseTeammateNode(const char* output_key) : output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;
    auto self = pm.GetSelf();

    if (!self || self->ship >= 8) return ExecuteResult::Failure;

    auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
    if (!opt_eg) return ExecuteResult::Failure;

    ExtremeGames* eg = *opt_eg;

    float enter_delay = ctx.bot->game->connection.settings.EnterDelay / 100.0f;

    // TODO: Find most forward in whichever direction.

    for (size_t i = 0; i < pm.player_count; ++i) {
      auto player = pm.players + i;

      if (player->id == self->id) continue;
      if (player->ship >= 8) continue;
      if (player->frequency != self->frequency) continue;
      if (player->enter_delay > 0.0f && player->enter_delay < enter_delay)
        continue;  // Let us lag attach but ignore if they are really dead.
      if (eg->GetBaseFromPosition(player->position) == -1) continue;

      ctx.blackboard.Set(output_key, player);

      return ExecuteResult::Success;
    }

    return ExecuteResult::Failure;
  }

  const char* output_key = nullptr;
};

struct FindNearestEnemyInBaseNode : public BehaviorNode {
  FindNearestEnemyInBaseNode(const char* output_key) : output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto& game = ctx.bot->game;

    Player* self = game->player_manager.GetSelf();
    if (!self) return ExecuteResult::Failure;

    RegionRegistry& region_registry = *ctx.bot->bot_controller->region_registry;

    Player* best_target = nullptr;
    float closest_dist_sq = std::numeric_limits<float>::max();

    auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
    if (!opt_eg) return ExecuteResult::Failure;

    ExtremeGames* eg = *opt_eg;

    for (size_t i = 0; i < game->player_manager.player_count; ++i) {
      Player* player = game->player_manager.players + i;

      if (player->ship >= 8) continue;
      if (player->frequency == self->frequency) continue;
      if (player->IsRespawning()) continue;
      if (player->position == Vector2f(0, 0)) continue;
      if (game->connection.map.GetTileId(player->position) == kTileIdSafe) continue;
      if (!region_registry.IsConnected(self->position, player->position)) continue;
      if (eg->GetBaseFromPosition(player->position) == -1) continue;

      float dist_sq = player->position.DistanceSq(self->position);
      if (dist_sq < closest_dist_sq) {
        closest_dist_sq = dist_sq;
        best_target = player;
      }
    }

    if (!best_target) {
      ctx.blackboard.Erase(output_key);
      return ExecuteResult::Failure;
    }

    ctx.blackboard.Set(output_key, best_target);
    return ExecuteResult::Success;
  }

  const char* output_key = nullptr;
};

}  // namespace eg
}  // namespace zero
