#pragma once

#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

struct ShipQueryNode : public BehaviorNode {
  ShipQueryNode(int ship) : ship(ship) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();

    if (self && self->ship == this->ship) {
      return ExecuteResult::Success;
    }

    return ExecuteResult::Failure;
  }

  int ship;
};

struct ShipRequestNode : public BehaviorNode {
  ShipRequestNode(int ship) : ship(ship) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    constexpr s32 kRequestInterval = 300;
    constexpr const char* kLastRequestKey = "last_ship_request_tick";

    auto self = ctx.bot->game->player_manager.GetSelf();

    if (!self) return ExecuteResult::Failure;
    if (self->ship == this->ship) return ExecuteResult::Success;

    s32 last_request_tick = ctx.blackboard.ValueOr<s32>(kLastRequestKey, 0);
    s32 current_tick = GetCurrentTick();

    if (TICK_DIFF(current_tick, last_request_tick) >= kRequestInterval) {
      printf("Sending ship request\n");

      ctx.bot->game->connection.SendShipRequest(ship);

      ctx.blackboard.Set(kLastRequestKey, current_tick);

      return ExecuteResult::Running;
    }

    return ExecuteResult::Failure;
  }

  int ship;
};

}  // namespace behavior
}  // namespace zero
