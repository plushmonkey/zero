#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

struct RegionContainQueryNode : public BehaviorNode {
  RegionContainQueryNode(Vector2f position) : coord(position) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    auto& registry = ctx.bot->bot_controller->region_registry;

    if (!registry) return ExecuteResult::Failure;

    MapCoord self_coord(self->position);

    bool connected = registry->IsConnected(self_coord, coord);

    return connected ? ExecuteResult::Success : ExecuteResult::Failure;
  }

  MapCoord coord;
};

}  // namespace behavior
}  // namespace zero
