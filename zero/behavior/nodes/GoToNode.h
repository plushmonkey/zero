#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

struct GoToNode : public BehaviorNode {
  GoToNode(const char* position_key) : position_key(position_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return ExecuteResult::Failure;

    auto opt_pos = ctx.blackboard.Value<Vector2f>(position_key);
    if (!opt_pos.has_value()) return ExecuteResult::Failure;

    Vector2f& target = opt_pos.value();
    auto& map = ctx.bot->game->GetMap();

    CastResult cast = map.CastTo(self->position, target, self->frequency);

    if (!cast.hit) {
      ctx.bot->bot_controller->steering.Seek(*ctx.bot->game, target);

      return ExecuteResult::Success;
    }

    return ExecuteResult::Failure;
  }

  const char* position_key;
};

}  // namespace behavior
}  // namespace zero
