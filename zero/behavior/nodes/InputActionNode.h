#pragma once

#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>
#include <zero/BotController.h>

namespace zero {
namespace behavior {

struct InputActionNode : public BehaviorNode {
  InputActionNode(InputAction action) : action(action) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();

    if (!self) return ExecuteResult::Failure;

    ctx.bot->bot_controller->input->SetAction(action, true);
    
    return ExecuteResult::Success;
  }

  InputAction action;
};

}  // namespace behavior
}  // namespace zero
