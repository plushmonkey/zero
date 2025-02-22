#pragma once

#include <zero/BotController.h>
#include <zero/RegionRegistry.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

#include <unordered_set>

namespace zero {
namespace svs {

struct IncomingDamageQueryNode : public behavior::BehaviorNode {
  IncomingDamageQueryNode(float distance, const char* output_key) : distance(distance), output_key(output_key) {}
  IncomingDamageQueryNode(const char* distance_key, const char* output_key)
      : distance_key(distance_key), output_key(output_key) {}
  IncomingDamageQueryNode(const char* player_key, const char* distance_key, const char* output_key)
      : player_key(player_key), distance_key(distance_key), output_key(output_key) {}
  IncomingDamageQueryNode(const char* player_key, float distance, float radius_multiplier, const char* output_key)
      : player_key(player_key), distance(distance), radius_multiplier(radius_multiplier), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* player = ctx.bot->game->player_manager.GetSelf();

    if (player_key) {
      auto opt_player = ctx.blackboard.Value<Player*>(player_key);
      if (!opt_player) return behavior::ExecuteResult::Failure;
      player = *opt_player;
    }

    if (!player) return behavior::ExecuteResult::Failure;
    if (player->ship >= 8) return behavior::ExecuteResult::Failure;

    float check_distance = distance;
    if (distance_key != nullptr) {
      auto opt_distance = ctx.blackboard.Value<float>(distance_key);
      if (!opt_distance) return behavior::ExecuteResult::Failure;

      check_distance = *opt_distance;
    }

    float distance_sq = check_distance * check_distance;
    float ship_radius = ctx.bot->game->connection.settings.ShipSettings[player->ship].GetRadius() * radius_multiplier;
    float bounds_extent = ship_radius * 2.0f;

    Rectangle self_bounds(player->position - Vector2f(bounds_extent, bounds_extent),
                          player->position + Vector2f(bounds_extent, bounds_extent));

    float total_damage = 0.0f;

    links.clear();

    auto& weapon_man = ctx.bot->game->weapon_manager;
    for (size_t i = 0; i < weapon_man.weapon_count; ++i) {
      Weapon& weapon = weapon_man.weapons[i];

      if (weapon.frequency == player->frequency) continue;
      if (weapon.data.type == WeaponType::Repel || weapon.data.type == WeaponType::Decoy) continue;
      if (weapon.data.type == WeaponType::Burst && !(weapon.flags & WEAPON_FLAG_BURST_ACTIVE)) continue;
      if (weapon.position.DistanceSq(player->position) > distance_sq) continue;
      if (links.contains(weapon.link_id)) continue;

      float dist = 0.0f;

      Vector2f relative_velocity = weapon.velocity - player->velocity;
      Rectangle check_bounds = self_bounds;

      if ((weapon.data.type == WeaponType::Bomb || weapon.data.type == WeaponType::ProximityBomb) &&
          weapon.velocity.LengthSq() <= 0.0f) {
        check_bounds = self_bounds.Scale(3.0f);
      }

      if (RayBoxIntersect(Ray(weapon.position, Normalize(relative_velocity)), check_bounds, &dist, nullptr)) {
        total_damage += (float)GetEstimatedWeaponDamage(weapon, ctx.bot->game->connection);
      }

      if (weapon.link_id != kInvalidLink) {
        links.insert(weapon.link_id);
      }
    }

    ctx.blackboard.Set(output_key, total_damage);

    return behavior::ExecuteResult::Success;
  }

  float distance = 0.0f;
  float radius_multiplier = 1.0f;
  const char* player_key = nullptr;
  const char* distance_key = nullptr;
  const char* output_key = nullptr;

  // Keep the links set here so the memory can be reused.
  std::unordered_set<u32> links;
};

}  // namespace svs
}  // namespace zero
