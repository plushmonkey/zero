#pragma once

#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

struct GetPlayerPositionNode : public behavior::BehaviorNode {
  GetPlayerPositionNode(const char* player_key, const char* position_key)
      : player_key(player_key), position_key(position_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto player_opt = ctx.blackboard.Value<Player*>(player_key);
    if (!player_opt.has_value()) return behavior::ExecuteResult::Failure;

    ctx.blackboard.Set(position_key, player_opt.value()->position);

    return behavior::ExecuteResult::Success;
  }

  const char* player_key;
  const char* position_key;
};

}  // namespace behavior
}  // namespace zero
