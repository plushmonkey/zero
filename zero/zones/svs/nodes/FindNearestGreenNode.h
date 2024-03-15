#pragma once

#include <zero/BotController.h>
#include <zero/RegionRegistry.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace svs {

struct FindNearestGreenNode : public behavior::BehaviorNode {
  FindNearestGreenNode(const char* output_key) : output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    PrizeGreen* nearest_green = nullptr;
    float nearest_distance_sq = 1024.0f * 1024.0f;

    for (size_t i = 0; i < ctx.bot->game->green_count; ++i) {
      PrizeGreen* green = ctx.bot->game->greens + i;

      float dist_sq = green->position.DistanceSq(self->position);
      if (dist_sq < nearest_distance_sq) {
        nearest_distance_sq = dist_sq;
        nearest_green = green;
      }
    }

    if (!nearest_green) return behavior::ExecuteResult::Failure;

    ctx.blackboard.Set(output_key, nearest_green->position);

    return behavior::ExecuteResult::Success;
  }

  const char* output_key = nullptr;
};

}  // namespace svs
}  // namespace zero
