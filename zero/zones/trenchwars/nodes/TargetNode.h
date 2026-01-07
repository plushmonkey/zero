#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>
#include <zero/zones/trenchwars/TrenchWars.h>

namespace zero {
namespace tw {

// Finds the nearest target that prioritizes self's sector or above.
// Will target sector below if none viable.
// Requires TrenchWars* "tw" key to function.
// If obey_stealth is true, then we will ignore players that we can't see.
struct NearestTargetPrioritizeSectorNode : public behavior::BehaviorNode {
  NearestTargetPrioritizeSectorNode(const char* player_key, const char* sector_key, bool obey_stealth = false)
      : player_key(player_key), sector_key(sector_key), obey_stealth(obey_stealth) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return behavior::ExecuteResult::Failure;

    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;

    TrenchWars* tw = *opt_tw;

    Sector self_sector = tw->GetDefiniteSector(self->position);
    Player* nearest = GetNearestTarget(tw, self_sector, ctx, *self);

    if (!nearest) {
      ctx.blackboard.Erase(player_key);
      return behavior::ExecuteResult::Failure;
    }

    ctx.blackboard.Set(player_key, nearest);

    return behavior::ExecuteResult::Success;
  }

  inline static bool IsVisible(ArenaSettings& settings, const Player& self, const Player& target) {
    constexpr Vector2f kViewDim(1920.0f, 1080.0f);

    // XRadar can see no matter what.
    if (self.togglables & Status_XRadar) return true;

    // We can always see them if they don't have stealth on.
    if (!(target.togglables & Status_Stealth)) return true;

    const Vector2f half_view_dim = kViewDim * 0.5f;

    Rectangle view_rect(self.position - half_view_dim, self.position + half_view_dim);
    Rectangle target_rect =
        Rectangle::FromPositionRadius(target.position, settings.ShipSettings[target.ship].GetRadius());

    // Target has stealth on and is off screen. We cannot see them.
    if (!BoxBoxIntersect(view_rect.min, view_rect.max, target_rect.min, target_rect.max)) {
      return false;
    }

    // We can see them if they don't have cloak on since they are on our screen.
    return !(target.togglables & Status_Cloak);
  }

 private:
  Player* GetNearestTarget(TrenchWars* tw, Sector sector, behavior::ExecuteContext& ctx, Player& self) {
    Game& game = *ctx.bot->game;
    RegionRegistry& region_registry = *ctx.bot->bot_controller->region_registry;

    Player* best_target = nullptr;
    float closest_dist_sq = std::numeric_limits<float>::max();

    Player* best_target_prio = nullptr;
    float closest_dist_prio_sq = std::numeric_limits<float>::max();

    for (size_t i = 0; i < game.player_manager.player_count; ++i) {
      Player* player = game.player_manager.players + i;

      if (player->ship >= 8) continue;
      if (player->frequency == self.frequency) continue;
      if (player->IsRespawning()) continue;
      if (player->position == Vector2f(0, 0)) continue;
      if (!game.player_manager.IsSynchronized(*player)) continue;
      if (!region_registry.IsConnected(self.position, player->position)) continue;

      bool in_safe = game.connection.map.GetTileId(player->position) == kTileIdSafe;
      if (in_safe) continue;

      if (obey_stealth && !IsVisible(ctx.bot->game->connection.settings, self, *player)) continue;

      float dist_sq = player->position.DistanceSq(self.position);

      Sector player_sector = tw->GetDefiniteSector(player->position);
      bool priority = !IsSectorAbove(sector, player_sector);

      if (priority) {
        if (!best_target_prio || dist_sq < closest_dist_prio_sq) {
          closest_dist_prio_sq = dist_sq;
          best_target_prio = player;
        }
      }

      if (dist_sq < closest_dist_sq) {
        closest_dist_sq = dist_sq;
        best_target = player;
      }
    }

    if (best_target_prio) return best_target_prio;

    return best_target;
  }

  bool obey_stealth = false;
  const char* sector_key = nullptr;
  const char* player_key = nullptr;
};

}  // namespace tw
}  // namespace zero