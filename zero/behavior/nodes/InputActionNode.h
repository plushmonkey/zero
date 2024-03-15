#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

struct InputActionNode : public BehaviorNode {
  InputActionNode(InputAction action) : action(action) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return ExecuteResult::Failure;

    auto& input = ctx.bot->bot_controller->input;
    if (!input) return ExecuteResult::Failure;

    input->SetAction(action, true);

    // Activate the callbacks so special action keys are handled like ship togglables.
    input->OnAction(action);

    return ExecuteResult::Success;
  }

  InputAction action;
};

// Returns Success if the input type is currently activated.
struct InputQueryNode : public BehaviorNode {
  InputQueryNode(InputAction action) : action(action) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return ExecuteResult::Failure;

    auto& input = ctx.bot->bot_controller->input;
    if (!input) return ExecuteResult::Failure;

    return input->IsDown(action) ? ExecuteResult::Success : ExecuteResult::Failure;
  }

  InputAction action;
};

struct WarpNode : public BehaviorNode {
  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return ExecuteResult::Failure;

    auto& input = ctx.bot->bot_controller->input;
    if (!input) return ExecuteResult::Failure;

    bool press = true;

    // Handle warp in a special way because it only activates on initial press down.
    if (ctx.blackboard.ValueOr("was_warping", false)) {
      press = false;
    }

    ctx.blackboard.Set("was_warping", press);

    input->SetAction(InputAction::Warp, press);

    return ExecuteResult::Success;
  }
};

}  // namespace behavior
}  // namespace zero
