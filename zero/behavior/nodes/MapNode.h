#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

struct PositionVisibleNode : public BehaviorNode {
  PositionVisibleNode(const char* position_key) : position_key(position_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();

    if (!self) return ExecuteResult::Failure;

    auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
    if (!opt_position.has_value()) return ExecuteResult::Failure;

    Vector2f& position = opt_position.value();

    bool hit = ctx.bot->game->GetMap().CastTo(self->position, position, self->frequency).hit;

    return hit ? ExecuteResult::Failure : ExecuteResult::Success;
  }

  const char* position_key;
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
