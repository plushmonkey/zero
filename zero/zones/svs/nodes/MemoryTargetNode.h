#pragma once

#include <zero/BotController.h>
#include <zero/RegionRegistry.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/TargetNode.h>
#include <zero/game/Game.h>

namespace zero {
namespace svs {

// If obey_stealth is true, then we will ignore players that we can't see.
// This is a specialized version of NearestTargetNode where it might choose to go back to the last place it saw someone.
struct NearestMemoryTargetNode : public behavior::BehaviorNode {
  NearestMemoryTargetNode(const char* player_key, bool obey_stealth = false)
      : player_key(player_key), obey_stealth(obey_stealth) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return behavior::ExecuteResult::Failure;

    Player* nearest = GetNearestTarget(*ctx.bot->game, *self, *ctx.bot->bot_controller->region_registry);

    if (!nearest) return behavior::ExecuteResult::Failure;

    ctx.blackboard.Set(player_key, nearest);

    return behavior::ExecuteResult::Success;
  }

 private:
  Player* GetNearestTarget(Game& game, Player& self, RegionRegistry& region_registry) {
    Player* best_target = nullptr;
    float closest_dist_sq = std::numeric_limits<float>::max();

    for (size_t i = 0; i < game.player_manager.player_count; ++i) {
      Player* player = game.player_manager.players + i;

      if (player->ship >= 8) continue;
      if (player->frequency == self.frequency) continue;
      if (player->IsRespawning()) continue;
      if (player->position == Vector2f(0, 0)) continue;
      if (!IsSynchronized(game, *player)) continue;
      if (!region_registry.IsConnected(self.position, player->position)) continue;

      bool in_safe = game.connection.map.GetTileId(player->position) == kTileIdSafe;
      if (in_safe) continue;

      if (obey_stealth && !behavior::NearestTargetNode::IsVisible(game.connection.settings, self, *player)) continue;

      float dist_sq = player->position.DistanceSq(self.position);
      if (dist_sq < closest_dist_sq) {
        closest_dist_sq = dist_sq;
        best_target = player;
      }
    }

    return best_target;
  }

  inline bool IsSynchronized(Game& game, Player& player) {
    // If the player is within our view, but we haven't received any packets, then they left where we last saw them and
    // should be ignored.
    if (game.radar.InRadarView(player.position)) {
      return game.player_manager.IsSynchronized(player);
    }

    // Try to path to where we last saw the player until their old position is in view.
    return true;
  }

  bool obey_stealth = false;
  const char* player_key;
};

}  // namespace svs
}  // namespace zero
