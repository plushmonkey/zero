#pragma once

#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>
#include <zero/game/Logger.h>

namespace zero {
namespace behavior {

struct ShipQueryNode : public BehaviorNode {
  ShipQueryNode(int ship) : ship(ship) {}
  ShipQueryNode(const char* ship_key) : ship_key(ship_key) {}
  ShipQueryNode(const char* player_key, int ship) : player_key(player_key), ship(ship) {}
  ShipQueryNode(const char* player_key, const char* ship_key) : player_key(player_key), ship_key(ship_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto player = ctx.bot->game->player_manager.GetSelf();

    if (player_key) {
      auto opt_player = ctx.blackboard.Value<Player*>(player_key);
      if (!opt_player.has_value()) return ExecuteResult::Failure;

      player = opt_player.value();
    }

    if (!player) return ExecuteResult::Failure;

    int check_ship = this->ship;

    if (ship_key) {
      auto opt_ship = ctx.blackboard.Value<int>(ship_key);
      if (!opt_ship.has_value()) return ExecuteResult::Failure;

      check_ship = opt_ship.value();
    }

    if (check_ship < 0 || check_ship > 8) return ExecuteResult::Failure;

    if (player->ship == check_ship) {
      return ExecuteResult::Success;
    }

    return ExecuteResult::Failure;
  }

  int ship = 0;

  const char* player_key = nullptr;
  const char* ship_key = nullptr;
};

struct ShipRequestNode : public BehaviorNode {
  ShipRequestNode(int ship) : ship(ship) {}
  ShipRequestNode(const char* ship_key) : ship_key(ship_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    constexpr s32 kRequestInterval = 300;
    constexpr const char* kLastRequestKey = "last_ship_request_tick";

    auto self = ctx.bot->game->player_manager.GetSelf();

    if (!self) return ExecuteResult::Failure;

    int requested_ship = this->ship;

    if (ship_key) {
      auto opt_ship = ctx.blackboard.Value<int>(ship_key);
      if (!opt_ship.has_value()) return ExecuteResult::Failure;

      requested_ship = opt_ship.value();
    }

    if (self->ship == requested_ship) return ExecuteResult::Success;

    s32 last_request_tick = ctx.blackboard.ValueOr<s32>(kLastRequestKey, 0);
    s32 current_tick = GetCurrentTick();
    s32 next_allowed_tick = MAKE_TICK(last_request_tick + kRequestInterval);

    bool allowed = TICK_GTE(current_tick, next_allowed_tick);

    if (!ctx.blackboard.Has(kLastRequestKey)) {
      allowed = true;
    }

    if (allowed) {
      Log(LogLevel::Info, "Sending ship request for %d.", requested_ship);

      ctx.bot->game->connection.SendShipRequest(requested_ship);

      ctx.blackboard.Set(kLastRequestKey, current_tick);

      return ExecuteResult::Running;
    }

    return ExecuteResult::Failure;
  }

  int ship = 0;
  const char* ship_key = nullptr;
};

}  // namespace behavior
}  // namespace zero
