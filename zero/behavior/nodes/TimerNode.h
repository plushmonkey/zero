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
  TimerSetNode(const char* timer_key, u32 ticks) : timer_key(timer_key), ticks(ticks) {}
  TimerSetNode(const char* timer_key, const char* ticks_key) : timer_key(timer_key), ticks_key(ticks_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    u32 set_ticks = ticks;

    if (ticks_key) {
      auto opt_ticks = ctx.blackboard.Value<u32>(ticks_key);
      if (!opt_ticks) return ExecuteResult::Failure;
      set_ticks = *opt_ticks;
    }

    ctx.blackboard.Set<u32>(timer_key, GetCurrentTick() + set_ticks);

    return ExecuteResult::Success;
  }

  const char* timer_key = nullptr;
  const char* ticks_key = nullptr;

  u32 ticks = 0;
};

}  // namespace behavior
}  // namespace zero
