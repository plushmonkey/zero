#pragma once

#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/zones/trenchwars/TrenchWars.h>

namespace zero {
namespace tw {

#if TW_RENDER_FR

struct RenderFlagroomNode : public behavior::BehaviorNode {
  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto& world_camera = ctx.bot->game->camera;
    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;

    auto tw = *opt_tw;
    bool render = false;

    for (auto& pos : tw->fr_positions) {
      Vector2f start(pos.x, pos.y);
      Vector2f end(pos.x + 1.0f, pos.y + 1.0f);
      Vector3f color(0.0f, 1.0f, 0.0f);

      ctx.bot->game->line_renderer.PushRect(start, end, color);
      render = true;
    }

    if (render) {
      ctx.bot->game->line_renderer.Render(world_camera);
    }

    return behavior::ExecuteResult::Success;
  }
};

#endif

// Determines if we are the best player to be claiming a flag.
// It's not perfect since it is distance, not path distance, but it's fast.
struct BestFlagClaimerNode : public behavior::BehaviorNode {
  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;
    auto self = pm.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    Player* best_player = nullptr;
    float best_dist_sq = 1024.0f * 1024.0f;

    Vector2f flag_position = ctx.blackboard.ValueOr("tw_flag_position", Vector2f(512, 269));

    // Loop over players to find a teammate that can be attached to that is closer to the flag room than us.
    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* player = pm.players + i;

      if (player->ship >= 8) continue;
      if (player->frequency != self->frequency) continue;

      float dist_sq = player->position.DistanceSq(flag_position);
      if (dist_sq < best_dist_sq) {
        best_dist_sq = dist_sq;
        best_player = player;
      }
    }

    if (best_player && best_player->id == self->id) {
      return behavior::ExecuteResult::Success;
    }

    return behavior::ExecuteResult::Failure;
  }
};

}  // namespace tw
}  // namespace zero
