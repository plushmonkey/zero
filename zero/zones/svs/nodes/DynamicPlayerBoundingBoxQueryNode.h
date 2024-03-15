#pragma once

#include <zero/BotController.h>
#include <zero/RegionRegistry.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace svs {

// Dynamically scale the target bounding box to require more precise aim based on energy percent.
struct DynamicPlayerBoundingBoxQueryNode : public behavior::BehaviorNode {
  DynamicPlayerBoundingBoxQueryNode(const char* output_key, float max_radius_multiplier = 1.0f)
      : player_key(nullptr), output_key(output_key), max_radius_multiplier(max_radius_multiplier) {}
  DynamicPlayerBoundingBoxQueryNode(const char* player_key, const char* output_key, float max_radius_multiplier = 1.0f)
      : player_key(player_key), output_key(output_key), max_radius_multiplier(max_radius_multiplier) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* player = ctx.bot->game->player_manager.GetSelf();

    if (player_key != nullptr) {
      auto player_opt = ctx.blackboard.Value<Player*>(player_key);
      if (!player_opt.has_value()) return behavior::ExecuteResult::Failure;

      player = player_opt.value();
    }

    if (!player) return behavior::ExecuteResult::Failure;
    if (player->ship >= 8) return behavior::ExecuteResult::Failure;

    Player* self = ctx.bot->game->player_manager.GetSelf();
    float max_energy = (float)ctx.bot->game->ship_controller.ship.energy;

    // TODO: Could scale based on distance from enemy as well.
    float scalar = (self->energy / max_energy);
    float radius_multiplier = 1.0f + (max_radius_multiplier - 1.0f) * scalar;

    float radius = ctx.bot->game->connection.settings.ShipSettings[player->ship].GetRadius() * radius_multiplier;

    Rectangle bounds;

    bounds.min = player->position - Vector2f(radius, radius);
    bounds.max = player->position + Vector2f(radius, radius);

    ctx.blackboard.Set(output_key, bounds);

    return behavior::ExecuteResult::Success;
  }

  const char* player_key;
  const char* output_key;
  float max_radius_multiplier;
};

}  // namespace svs
}  // namespace zero
