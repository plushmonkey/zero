#pragma once

#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

struct TimerExpiredNode : public BehaviorNode {
  TimerExpiredNode(const char* key) : key(key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    if (!ctx.blackboard.Has(key)) return ExecuteResult::Success;

    u32 timeout = ctx.blackboard.ValueOr<u32>(key, 0);
    u32 current_tick = GetCurrentTick();

    return TICK_GTE(current_tick, timeout) ? ExecuteResult::Success : ExecuteResult::Failure;
  }

  const char* key;
};

struct TimerSetNode : public BehaviorNode {
  TimerSetNode(const char* key, u32 ticks) : key(key), ticks(ticks) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    ctx.blackboard.Set<u32>(key, GetCurrentTick() + ticks);

    return ExecuteResult::Success;
  }

  const char* key;
  u32 ticks;
};

}  // namespace behavior
}  // namespace zero
