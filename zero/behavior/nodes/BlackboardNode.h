#pragma once

#include <stdio.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>
#include <zero/game/Logger.h>

namespace zero {
namespace behavior {

struct DebugPrintNode : public BehaviorNode {
  DebugPrintNode(const char* message, LogLevel level = LogLevel::Debug) : message(message), level(level) {}
  DebugPrintNode(LogLevel level, const char* message) : message(message), level(level) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    Log(level, "%s", message);
    return ExecuteResult::Success;
  }

  const char* message;
  LogLevel level;
};

struct BlackboardSetQueryNode : public BehaviorNode {
  BlackboardSetQueryNode(const char* key) : key(key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    bool has = ctx.blackboard.Has(key);

    return has ? ExecuteResult::Success : ExecuteResult::Failure;
  }

  const char* key;
};

template <typename T>
struct ValueCompareQuery : public behavior::BehaviorNode {
  ValueCompareQuery(const char* key, T compare_value) : key(key), compare_value(compare_value) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto opt_value = ctx.blackboard.Value<T>(key);
    if (!opt_value.has_value()) return behavior::ExecuteResult::Failure;

    T current_value = opt_value.value();

    return current_value == compare_value ? behavior::ExecuteResult::Success : behavior::ExecuteResult::Failure;
  }

  const char* key;
  T compare_value;
};

}  // namespace behavior
}  // namespace zero
