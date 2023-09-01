#pragma once

#include <stdio.h>
#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>
#include <zero/game/Logger.h>

namespace zero {
namespace behavior {

struct BehaviorSetNode : public BehaviorNode {
  BehaviorSetNode(const char* behavior_name) : behavior_name(behavior_name) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto behavior = ctx.bot->bot_controller->behaviors.Find(behavior_name);

    if (!behavior) return ExecuteResult::Failure;

    behavior->OnInitialize(ctx);
    ctx.bot->bot_controller->behavior_tree = behavior->CreateTree(ctx);

    return ExecuteResult::Success;
  }

  const char* behavior_name;
};

}  // namespace behavior
}  // namespace zero
