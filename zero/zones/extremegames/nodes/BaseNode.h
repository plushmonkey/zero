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

    ctx.blackboard.Set<u16>(output_key, eg->base_states[base_index].controlling_freq);

    return ExecuteResult::Success;
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

// Find the entrance position of our team's existing base.
// If our team doesn't have a base, it will default to a new base that every other bot will generate similarly.
struct FindTeamBaseEntranceNode : public BehaviorNode {
  FindTeamBaseEntranceNode(const char* output_key) : output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;
    auto self = pm.GetSelf();

    if (!self || self->ship >= 8) return ExecuteResult::Failure;

    auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
    if (!opt_eg) return ExecuteResult::Failure;

    ExtremeGames* eg = *opt_eg;

    for (size_t i = 0; i < eg->base_states.size(); ++i) {
      BaseState& state = eg->base_states[i];

      if (state.controlling_freq == self->frequency && state.GetDefendingFlagCount() > 0) {
        ctx.blackboard.Set(output_key, eg->bases[i].entrance_position);
        return ExecuteResult::Success;
      }
    }

    size_t base_index = GetCurrentDefaultBase(eg->bases.size());

    ctx.blackboard.Set(output_key, eg->bases[base_index].entrance_position);

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

struct FindEnemyBaseEntranceNode : public BehaviorNode {
  FindEnemyBaseEntranceNode(const char* output_key) : output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;
    auto self = pm.GetSelf();

    if (!self || self->ship >= 8) return ExecuteResult::Failure;

    auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
    if (!opt_eg) return ExecuteResult::Failure;

    ExtremeGames* eg = *opt_eg;

    for (size_t i = 0; i < eg->base_states.size(); ++i) {
      BaseState& state = eg->base_states[i];

      if (state.IsEnemyControlled(self->frequency) && state.GetDefendingFlagCount() > 0) {
        ctx.blackboard.Set(output_key, eg->bases[i].entrance_position);
        return ExecuteResult::Success;
      }
    }

    return ExecuteResult::Failure;
  }

  const char* output_key = nullptr;
};

struct SelectAttackingTeammateNode : public BehaviorNode {
  SelectAttackingTeammateNode(const char* in_base_position_key, const char* anchor_key, const char* output_key)
      : in_base_position_key(in_base_position_key), anchor_key(anchor_key), output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;
    auto self = pm.GetSelf();

    if (!self || self->ship >= 8) return ExecuteResult::Failure;

    auto opt_base_position = ctx.blackboard.Value<Vector2f>(in_base_position_key);
    if (!opt_base_position) return ExecuteResult::Failure;

    auto opt_anchor = ctx.blackboard.Value<Player*>(anchor_key);
    if (!opt_anchor) return ExecuteResult::Failure;

    auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
    if (!opt_eg) return ExecuteResult::Failure;

    // TODO: Randomize teammate selection with some probability.
    Vector2f base_position = *opt_base_position;
    Player* anchor = *opt_anchor;
    ExtremeGames* eg = *opt_eg;

    Player* target = anchor;

    if (!eg->IsPositionInsideBase(anchor->position)) {
      // Our anchor isn't in the base, so try to find the best
      size_t base_index = eg->GetBaseFromPosition(base_position);

      if (base_index < eg->bases.size()) {
        float best_percent = 0.0f;

        // Go through each player in the target base and find deepest teammate.
        for (PlayerBaseState& state : eg->base_states[base_index].player_data) {
          if (state.frequency == self->frequency && state.position_percent >= best_percent) {
            best_percent = state.position_percent;
            target = ctx.bot->game->player_manager.GetPlayerById(state.player_id);
          }
        }
      }
    }

    ctx.blackboard.Set(output_key, target);

    return ExecuteResult::Success;
  }

  const char* in_base_position_key = nullptr;
  const char* anchor_key = nullptr;
  const char* output_key = nullptr;
};

struct SelectDefendingTeammateNode : public BehaviorNode {
  SelectDefendingTeammateNode(const char* anchor_key, const char* output_key)
      : anchor_key(anchor_key), output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;
    auto self = pm.GetSelf();

    if (!self || self->ship >= 8) return ExecuteResult::Failure;

    auto opt_anchor = ctx.blackboard.Value<Player*>(anchor_key);
    if (!opt_anchor) return ExecuteResult::Failure;

    auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
    if (!opt_eg) return ExecuteResult::Failure;

    ExtremeGames* eg = *opt_eg;

    // TODO: Randomize teammate selection with some probability.

    ctx.blackboard.Set(output_key, *opt_anchor);

    return ExecuteResult::Success;
  }

  const char* anchor_key = nullptr;
  const char* output_key = nullptr;
};

struct FindNearestEnemyInBaseNode : public BehaviorNode {
  FindNearestEnemyInBaseNode(const char* output_key) : output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto& game = ctx.bot->game;

    Player* self = game->player_manager.GetSelf();
    if (!self) return ExecuteResult::Failure;

    auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
    if (!opt_eg) return ExecuteResult::Failure;

    ExtremeGames* eg = *opt_eg;

    size_t base_index = eg->GetBaseFromPosition(self->position);
    if (base_index >= eg->base_states.size()) return ExecuteResult::Failure;

    BaseState& state = eg->base_states[base_index];

    float self_position = 0.0f;

    for (size_t i = 0; i < state.player_data.size(); ++i) {
      u16 pid = state.player_data[i].player_id;
      if (pid == self->id) {
        self_position = state.player_data[i].position_percent;
        break;
      }
    }

    Player* best_target = nullptr;
    float closest_dist = 1.0f;

    for (size_t i = 0; i < state.player_data.size(); ++i) {
      u16 pid = state.player_data[i].player_id;
      float position = state.player_data[i].position_percent;
      Player* player = game->player_manager.GetPlayerById(pid);

      float position_difference = fabsf(position - self_position);

      if (!player || player->ship >= 8) continue;
      if (player->frequency == self->frequency) continue;
      if (player->IsRespawning()) continue;
      if (game->connection.map.GetTileId(player->position) == kTileIdSafe) continue;

      if (position_difference < closest_dist) {
        closest_dist = position_difference;
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

struct UpdateBaseStateNode : public BehaviorNode {
  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
    if (!opt_eg) return ExecuteResult::Failure;

    ExtremeGames* eg = *opt_eg;

    eg->UpdateBaseState(*ctx.bot);

    return ExecuteResult::Success;
  }
};

// This will try to find a safe position for the anchor to sit around.
struct CalculateAnchorPositionNode : public BehaviorNode {
  CalculateAnchorPositionNode(const char* output_key) : output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return ExecuteResult::Failure;

    auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
    if (!opt_eg) return ExecuteResult::Failure;

    ExtremeGames* eg = *opt_eg;

    size_t base_index = eg->GetBaseFromPosition(self->position);
    if (base_index >= eg->bases.size()) return ExecuteResult::Failure;

    auto& base = eg->bases[base_index];
    auto& base_state = eg->base_states[base_index];

    // TODO: We should find a safe position by looking at the base path instead.
    constexpr const float kBehindPercentage = 0.03f;

    float target_penetration = base_state.attacking_penetration_percent;

    if (base_state.controlling_freq == self->frequency) {
      // If we are defending, we want to sit above the penetration depth
      target_penetration += kBehindPercentage;
    } else {
      // If we are attacking, we want to sit behind the penetration depth
      target_penetration -= kBehindPercentage;
    }

    Vector2f target_position = GetPenerationPosition(eg, base_index, target_penetration);

    ctx.blackboard.Set(output_key, target_position);

    return ExecuteResult::Success;
  }

  Vector2f GetPenerationPosition(ExtremeGames* eg, size_t base_index, float target_penetration) const {
    MapBase& base = eg->bases[base_index];

    if (base.path.Empty()) {
      return Vector2f(512, 512);
    }

    for (const Vector2f& position : base.path.points) {
      float position_percent = eg->GetBasePenetrationPercent(base_index, position);

      if (position_percent >= target_penetration) {
        return position;
      }
    }

    return base.path.points[0];
  }

  const char* output_key = nullptr;
};

}  // namespace eg
}  // namespace zero
