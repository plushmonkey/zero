#pragma once

#include <stdio.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

struct DebugPrintNode : public BehaviorNode {
  DebugPrintNode(const char* message) : message(message) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    printf("%s\n", message);
    return ExecuteResult::Success;
  }

  const char* message;
};

struct BlackboardSetQueryNode : public BehaviorNode {
  BlackboardSetQueryNode(const char* key) : key(key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    bool has = ctx.blackboard.Has(key);

    return has ? ExecuteResult::Success : ExecuteResult::Failure;
  }

  const char* key;
};

}  // namespace behavior
}  // namespace zero
