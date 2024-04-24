#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

struct RegionContainQueryNode : public BehaviorNode {
  RegionContainQueryNode(Vector2f position) : coord(position) {}
  RegionContainQueryNode(const char* position_key) : position_key(position_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    auto& registry = ctx.bot->bot_controller->region_registry;

    if (!registry) return ExecuteResult::Failure;

    MapCoord self_coord(self->position);
    MapCoord check_coord = this->coord;

    if (position_key) {
      auto opt_position = ctx.blackboard.Value<MapCoord>(position_key);
      if (opt_position) {
        check_coord = *opt_position;
      } else {
        auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
        if (opt_position) {
          check_coord = *opt_position;
        } else {
          return ExecuteResult::Failure;
        }
      }
    }

    bool connected = registry->IsConnected(self_coord, check_coord);

    return connected ? ExecuteResult::Success : ExecuteResult::Failure;
  }

  MapCoord coord;
  const char* position_key = nullptr;
};

}  // namespace behavior
}  // namespace zero
