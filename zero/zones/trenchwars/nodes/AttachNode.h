#pragma once

#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/zones/trenchwars/TrenchWars.h>

namespace zero {
namespace tw {

struct BestAttachQueryNode : public behavior::BehaviorNode {
  BestAttachQueryNode(const char* output_key) : output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;
    auto self = pm.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    Player* best_player = nullptr;
    float best_dist_sq = 1024.0f * 1024.0f;

    Vector2f flag_position = ctx.blackboard.ValueOr("tw_flag_position", Vector2f(512, 269));
    float self_dist_sq = self->position.DistanceSq(flag_position);

    // Loop over players to find a teammate that can be attached to that is closer to the flag room than us.
    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* player = pm.players + i;

      if (player->id == self->id) continue;
      if (player->ship >= 8) continue;
      if (player->frequency != self->frequency) continue;
      if (player->position.DistanceSq(flag_position) > self_dist_sq) continue;

      u8 turret_limit = ctx.bot->game->connection.settings.ShipSettings[player->ship].TurretLimit;

      if (turret_limit == 0) continue;

      u8 turret_count = 0;

      AttachInfo* child = player->children;
      while (child) {
        ++turret_count;
        child = child->next;
      }

      if (turret_count >= turret_limit) continue;

      float dist_sq = flag_position.DistanceSq(player->position);
      if (dist_sq < best_dist_sq) {
        best_dist_sq = dist_sq;
        best_player = player;
      }
    }

    if (!best_player) return behavior::ExecuteResult::Failure;

    ctx.blackboard.Set<Player*>(output_key, best_player);

    return behavior::ExecuteResult::Success;
  }

  const char* output_key = nullptr;
};

}  // namespace tw
}  // namespace zero
