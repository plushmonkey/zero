#pragma once

#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

struct PlayerBoundingBoxQueryNode : public behavior::BehaviorNode {
  PlayerBoundingBoxQueryNode(const char* output_key, float radius_multiplier = 1.0f)
      : player_key(nullptr), output_key(output_key), radius_multiplier(radius_multiplier) {}
  PlayerBoundingBoxQueryNode(const char* player_key, const char* output_key, float radius_multiplier = 1.0f)
      : player_key(player_key), output_key(output_key), radius_multiplier(radius_multiplier) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* player = ctx.bot->game->player_manager.GetSelf();

    if (player_key != nullptr) {
      auto player_opt = ctx.blackboard.Value<Player*>(player_key);
      if (!player_opt.has_value()) return behavior::ExecuteResult::Failure;

      player = player_opt.value();
    }

    if (!player) return behavior::ExecuteResult::Failure;
    if (player->ship >= 8) return behavior::ExecuteResult::Failure;

    float radius = ctx.bot->game->connection.settings.ShipSettings[player->ship].GetRadius() * radius_multiplier;

    Rectangle bounds;

    bounds.bottom_left = player->position - Vector2f(radius, radius);
    bounds.top_right = player->position + Vector2f(radius, radius);

    ctx.blackboard.Set(output_key, bounds);

    return behavior::ExecuteResult::Success;
  }

  const char* player_key;
  const char* output_key;
  float radius_multiplier;
};

struct PlayerStatusQueryNode : public behavior::BehaviorNode {
  PlayerStatusQueryNode(StatusFlag status) : player_key(nullptr), status(status) {}
  PlayerStatusQueryNode(const char* player_key, StatusFlag status) : player_key(player_key), status(status) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* player = ctx.bot->game->player_manager.GetSelf();

    if (player_key != nullptr) {
      auto player_opt = ctx.blackboard.Value<Player*>(player_key);
      if (!player_opt.has_value()) return behavior::ExecuteResult::Failure;

      player = player_opt.value();
    }

    if (!player) return behavior::ExecuteResult::Failure;

    if (player->togglables & status) {
      return behavior::ExecuteResult::Success;
    }

    return behavior::ExecuteResult::Failure;
  }

  const char* player_key;
  StatusFlag status;
};

struct PlayerPositionQueryNode : public behavior::BehaviorNode {
  PlayerPositionQueryNode(const char* position_key) : player_key(nullptr), position_key(position_key) {}
  PlayerPositionQueryNode(const char* player_key, const char* position_key)
      : player_key(player_key), position_key(position_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* player = ctx.bot->game->player_manager.GetSelf();

    if (player_key) {
      auto player_opt = ctx.blackboard.Value<Player*>(player_key);
      if (!player_opt.has_value()) return behavior::ExecuteResult::Failure;

      player = player_opt.value();
    }

    if (!player) {
      return behavior::ExecuteResult::Failure;
    }

    ctx.blackboard.Set(position_key, player->position);

    return behavior::ExecuteResult::Success;
  }

  const char* player_key;
  const char* position_key;
};

}  // namespace behavior
}  // namespace zero
