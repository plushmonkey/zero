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
    ctx.bot->bot_controller->SetBehavior(behavior_name, behavior->CreateTree(ctx));

    return ExecuteResult::Success;
  }

  const char* behavior_name;
};

struct BehaviorSetFromKeyNode : public BehaviorNode {
  BehaviorSetFromKeyNode(const char* key_name) : key_name(key_name) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto name_opt = ctx.blackboard.Value<std::string>(key_name);
    if (!name_opt.has_value()) return ExecuteResult::Failure;

    auto behavior = ctx.bot->bot_controller->behaviors.Find(name_opt.value());

    if (!behavior) return ExecuteResult::Failure;

    behavior->OnInitialize(ctx);
    ctx.bot->bot_controller->SetBehavior(name_opt.value(), behavior->CreateTree(ctx));

    return ExecuteResult::Success;
  }

  const char* key_name;
};

}  // namespace behavior
}  // namespace zero
