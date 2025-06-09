#pragma once

#include <zero/BotController.h>
#include <zero/RegionRegistry.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>
#include <zero/game/Logger.h>

namespace zero {
namespace nexus {

//Return nearest teammate position for the provided player to be used in distance threshold checks, including self
struct NearestTeammatePlayerPositionQueryNode : public behavior::BehaviorNode {
  NearestTeammatePlayerPositionQueryNode(const char* position_key) : player_key(nullptr), position_key(position_key) {}
  NearestTeammatePlayerPositionQueryNode(const char* player_key, const char* position_key)
      : player_key(player_key), position_key(position_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return behavior::ExecuteResult::Failure;
    
    Player* subject = nullptr;
    if (player_key) {
      auto player_opt = ctx.blackboard.Value<Player*>(player_key);
      if (!player_opt.has_value()) return behavior::ExecuteResult::Failure;

      subject = player_opt.value();
    }

    if (!subject) return behavior::ExecuteResult::Failure;

    Player* nearest_teammate =
        GetNearestTeammateToPlayer(*ctx.bot->game, *self, *ctx.bot->bot_controller->region_registry, *subject);
    ctx.blackboard.Set(position_key, nearest_teammate->position);

    return behavior::ExecuteResult::Success;


  }

  private:
  Player* GetNearestTeammateToPlayer(Game& game, Player& self, RegionRegistry& region_registry, Player& subject) {
    Player* nearest_teammate = nullptr;
    float closest_dist_sq = std::numeric_limits<float>::max();

    for (size_t i = 0; i < game.player_manager.player_count; ++i) {
      Player* player = game.player_manager.players + i;

      if (player->ship >= 8) continue;
      if (player->frequency != self.frequency) continue;
      if (player->IsRespawning()) continue;
      if (player->position == Vector2f(0, 0)) continue;
      if (!IsSynchronized(game, *player)) continue;
      if (!region_registry.IsConnected(self.position, player->position)) continue;

      bool in_safe = game.connection.map.GetTileId(player->position) == kTileIdSafe;
      if (in_safe) continue;

      float dist_sq = player->position.DistanceSq(subject.position);

      if (dist_sq < closest_dist_sq) {
        closest_dist_sq = dist_sq;
        nearest_teammate = player;
      }
    }

    return nearest_teammate;
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
  const char* player_key;
  const char* position_key;
};

}  // namespace nexus
}  // namespace zero
