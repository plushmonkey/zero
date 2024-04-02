#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/Behavior.h>

namespace zero {
namespace deva {

struct GetAttachTargetNode : public behavior::BehaviorNode {
  GetAttachTargetNode(Rectangle ignore_rect, const char* output_key)
      : ignore_rect(ignore_rect), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    constexpr float kLagAttachTime = 0.7f;

    auto& pm = ctx.bot->game->player_manager;

    auto self = pm.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* player = pm.players + i;

      if (player == self) continue;
      if (player->frequency != self->frequency) continue;
      if (player->enter_delay > 0.0f && player->explode_anim_t >= kLagAttachTime) continue;
      if (player->position.x == 0.0f && player->position.y == 0.0f) continue;
      if (ignore_rect.Contains(player->position)) continue;

      // TODO: Better selection. Just use first one for now.
      ctx.blackboard.Set(output_key, player);

      return behavior::ExecuteResult::Success;
    }

    return behavior::ExecuteResult::Failure;
  }

  Rectangle ignore_rect;
  const char* output_key = nullptr;
};

}  // namespace deva
}  // namespace zero
