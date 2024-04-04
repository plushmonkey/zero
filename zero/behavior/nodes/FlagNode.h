#pragma once

#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

struct FlagCarryCountQueryNode : public BehaviorNode {
  FlagCarryCountQueryNode(const char* output_key) : output_key(output_key) {}
  FlagCarryCountQueryNode(const char* player_key, const char* output_key)
      : player_key(player_key), output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto player = ctx.bot->game->player_manager.GetSelf();

    if (player_key) {
      auto opt_player = ctx.blackboard.Value<Player*>(player_key);
      if (!opt_player) return ExecuteResult::Failure;
      player = *opt_player;
    }

    if (!player) return ExecuteResult::Failure;

    ctx.blackboard.Set<u16>(output_key, player->flags);

    return ExecuteResult::Success;
  }

  const char* player_key = nullptr;
  const char* output_key = nullptr;
};

struct FlagPositionQueryNode : public BehaviorNode {
  FlagPositionQueryNode(const char* flag_key, const char* output_key) : flag_key(flag_key), output_key(output_key) {}
  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_flag = ctx.blackboard.Value<GameFlag*>(flag_key);
    if (!opt_flag || !*opt_flag) return ExecuteResult::Failure;

    ctx.blackboard.Set(output_key, (*opt_flag)->position);

    return ExecuteResult::Success;
  }

  const char* flag_key = nullptr;
  const char* output_key = nullptr;
};

struct NearestFlagNode : public behavior::BehaviorNode {
  enum class Type { Claimed, Unclaimed, Any };

  NearestFlagNode(Type type, const char* output_key) : type(type), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return behavior::ExecuteResult::Failure;

    GameFlag* nearest = GetNearestTarget(*ctx.bot->game, *self);
    if (!nearest) return behavior::ExecuteResult::Failure;

    ctx.blackboard.Set(output_key, nearest);

    return behavior::ExecuteResult::Success;
  }

 private:
  GameFlag* GetNearestTarget(Game& game, Player& self) {
    GameFlag* best_target = nullptr;
    float closest_dist_sq = std::numeric_limits<float>::max();

    for (size_t i = 0; i < game.flag_count; ++i) {
      GameFlag* flag = game.flags + i;

      if (!(flag->flags & GameFlag_Dropped) && !(flag->flags & GameFlag_Turf)) continue;
      if (type == Type::Unclaimed && flag->owner == self.frequency) continue;
      if (type == Type::Claimed && flag->owner != self.frequency) continue;

      float dist_sq = flag->position.DistanceSq(self.position);
      if (dist_sq < closest_dist_sq) {
        best_target = flag;
        closest_dist_sq = dist_sq;
      }
    }

    return best_target;
  }

  Type type = Type::Unclaimed;
  const char* output_key;
};

}  // namespace behavior
}  // namespace zero
