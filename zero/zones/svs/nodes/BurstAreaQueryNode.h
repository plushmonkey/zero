#pragma once

#include <zero/BotController.h>
#include <zero/RegionRegistry.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace svs {

struct BurstAreaQueryNode : public behavior::BehaviorNode {
  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto& map = ctx.bot->game->connection.map;

    static const Vector2f kDirections[] = {
        Vector2f(-1, 0),
        Vector2f(1, 0),
        Vector2f(0, -1),
        Vector2f(0, 1),
    };
    constexpr float kSearchDistance = 8.0f;

    for (auto direction : kDirections) {
      if (DirectionHasWall(map, self->position, direction, kSearchDistance, self->frequency)) {
        return behavior::ExecuteResult::Success;
      }
    }

    return behavior::ExecuteResult::Failure;
  }

  bool DirectionHasWall(const Map& map, Vector2f start, Vector2f direction, float distance, u32 frequency) const {
    for (int i = 0; i < (int)(distance + 0.5f); ++i) {
      Vector2f check = start + direction * (float)i;

      if (map.IsSolid(check, frequency)) {
        if (HasNeighbors(map, check, frequency, 2)) {
          return true;
        }
      }
    }

    return false;
  }

  bool HasNeighbors(const Map& map, Vector2f position, u32 frequency, size_t solid_requirement) const {
    size_t count = 0;

    for (s32 y = -1; y <= 1; ++y) {
      for (s32 x = -1; x <= 1; ++x) {
        Vector2f check = position + Vector2f((float)x, (float)y);

        if (map.IsSolid(check, frequency)) {
          ++count;
        }
      }
    }

    return count >= solid_requirement;
  }
};

}  // namespace svs
}  // namespace zero
