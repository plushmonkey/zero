#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

struct VisibilityQueryNode : public BehaviorNode {
  VisibilityQueryNode(const char* position_key) : position_a_key(position_key), position_b_key(nullptr) {}
  VisibilityQueryNode(const char* position_a_key, const char* position_b_key) : position_a_key(position_a_key), position_b_key(position_b_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();

    if (!self) return ExecuteResult::Failure;

    auto opt_position_a = ctx.blackboard.Value<Vector2f>(position_a_key);
    if (!opt_position_a.has_value()) return ExecuteResult::Failure;

    Vector2f& position_a = opt_position_a.value();

    if (position_b_key != nullptr) {
      auto opt_position_b = ctx.blackboard.Value<Vector2f>(position_b_key);
      if (!opt_position_b.has_value()) return ExecuteResult::Failure;

      Vector2f& position_b = opt_position_b.value();

      bool hit = ctx.bot->game->GetMap().CastTo(position_a, position_b, 0xFFFF).hit;

      return hit ? ExecuteResult::Failure : ExecuteResult::Success;
    }

    bool hit = ctx.bot->game->GetMap().CastTo(self->position, position_a, self->frequency).hit;

    return hit ? ExecuteResult::Failure : ExecuteResult::Success;
  }

  const char* position_a_key;
  const char* position_b_key;
};

struct TileQueryNode : public BehaviorNode {
  TileQueryNode(TileId id) : id(id) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return ExecuteResult::Failure;

    TileId occupied_id = ctx.bot->game->GetMap().GetTileId(self->position);

    return occupied_id == id ? ExecuteResult::Success : ExecuteResult::Failure;
  }

  TileId id;
};

}  // namespace behavior
}  // namespace zero
