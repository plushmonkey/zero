#pragma once

#include <stdio.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

#include <random>

// These nodes are pretty bad. They could be improved by projecting weapons through an influence map and finding a nearby tile on the map that is safe.

namespace zero {
namespace behavior {

// Tries to find a position near the enemy for camping around.
struct FindTerritoryPosition : public BehaviorNode {
  FindTerritoryPosition(const char* target_player_key, const char* nearby_distance_key, const char* output_key,
                        bool fresh = false)
      : target_player_key(target_player_key),
        nearby_distance_key(nearby_distance_key),
        output_key(output_key),
        fresh(fresh) {
    rng = std::mt19937(dev());
  }

  ExecuteResult Execute(ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();
    Player* player = nullptr;

    if (!self) return ExecuteResult::Failure;

    if (target_player_key != nullptr) {
      auto player_opt = ctx.blackboard.Value<Player*>(target_player_key);
      if (!player_opt.has_value()) return ExecuteResult::Failure;

      player = player_opt.value();
    }

    if (!player) return ExecuteResult::Failure;

    float nearby_distance = 0.0f;
    if (nearby_distance_key != nullptr) {
      auto nearby_opt = ctx.blackboard.Value<float>(nearby_distance_key);
      if (!nearby_opt.has_value()) return ExecuteResult::Failure;

      nearby_distance = nearby_opt.value();
    }

    if (nearby_distance <= 0.0f) return ExecuteResult::Failure;

    if (!fresh) {
      // Check if there's already an existing position and use that if appropriate.
      auto existing_opt = ctx.blackboard.Value<Vector2f>(output_key);
      if (existing_opt.has_value()) {
        Vector2f existing = existing_opt.value();

        if (existing.DistanceSq(player->position) <= nearby_distance * nearby_distance) {
          if (!ctx.bot->game->GetMap().CastTo(player->position, existing, self->frequency).hit) {
            return ExecuteResult::Success;
          }
        }
      }
    }

    Vector2f to_target = player->position - self->position;
    Vector2f cross = Normalize(Perpendicular(to_target));

    // Generate a random position along the dividing line
    std::uniform_real_distribution<float> lateral_rng(-nearby_distance, nearby_distance);
    float lateral_dist = lateral_rng(rng);

    std::uniform_real_distribution<float> forward_rng(nearby_distance * 0.6f, nearby_distance);
    float forward = forward_rng(rng);

    Vector2f forward_offset = (-Normalize(to_target)) * forward;
    Vector2f lateral_offset = cross * lateral_dist;

    Vector2f end_position = player->position + lateral_offset + forward_offset;

    if (ctx.bot->game->GetMap().CastTo(player->position, end_position, self->frequency).hit) {
      return ExecuteResult::Failure;
    }

    if (ctx.bot->game->GetMap().GetTileId(end_position) != kTileSafeId) {
      ctx.blackboard.Set(output_key, end_position);
    }

    return ExecuteResult::Success;
  }

  const char* target_player_key;
  const char* nearby_distance_key;
  const char* output_key;
  bool fresh = false;

  std::random_device dev;
  std::mt19937 rng;
};

struct PositionThreatQueryNode : public BehaviorNode {
  PositionThreatQueryNode(Vector2f position, const char* output_key, float seconds_lookahead, float radius)
      : set_position(position), output_key(output_key), seconds_lookahead(seconds_lookahead), radius(radius) {}
  PositionThreatQueryNode(const char* position_key, const char* output_key, float seconds_lookahead, float radius)
      : position_key(position_key), output_key(output_key), seconds_lookahead(seconds_lookahead), radius(radius) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    Vector2f position = this->set_position;

    if (position_key) {
      auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
      if (!opt_position.has_value()) return ExecuteResult::Failure;

      position = opt_position.value();
    }

    float threat = GetThreatValue(ctx, position);
    ctx.blackboard.Set(output_key, threat);

    return ExecuteResult::Success;
  }

  float GetThreatValue(ExecuteContext& ctx, const Vector2f& position) {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return 0.0f;
    if (self->IsRespawning()) return 100.0f;

    u16 freq = self->frequency;

    auto& weapon_manager = ctx.bot->game->weapon_manager;

    float energy = self->energy;
    auto& map = ctx.bot->game->GetMap();
    auto& connection = ctx.bot->game->connection;

    Rectangle bounds(position - Vector2f(radius, radius), position + Vector2f(radius, radius));

    for (size_t i = 0; i < weapon_manager.weapon_count; ++i) {
      Weapon& weapon = weapon_manager.weapons[i];

      if (weapon.frequency == freq) continue;
      if (weapon.data.type == WeaponType::Repel || weapon.data.type == WeaponType::Decoy) continue;

      float dist = 0.0f;
      Vector2f start = weapon.position;

      if (RayBoxIntersect(Ray(start, Normalize(weapon.velocity)), bounds, &dist, nullptr)) {
        Vector2f end = weapon.position + weapon.velocity * seconds_lookahead;

        if (start.DistanceSq(end) >= dist * dist) {
          int damage = GetEstimatedWeaponDamage(weapon, connection);
          energy -= damage;

          if (energy <= 0) break;
        }
      }
    }

    return (self->energy - energy) / ctx.bot->game->ship_controller.ship.energy;
  }

  int GetEstimatedWeaponDamage(Weapon& weapon, Connection& connection) {
    // This might be a dangerous weapon.
    // Estimate damage from this weapon.
    int damage = 0;

    switch (weapon.data.type) {
      case WeaponType::Bullet:
      case WeaponType::BouncingBullet: {
        if (weapon.data.shrap > 0) {
          s32 remaining = weapon.end_tick - GetCurrentTick();
          s32 duration = connection.settings.BulletAliveTime - remaining;

          if (duration <= 25) {
            damage = connection.settings.InactiveShrapDamage / 1000;
          } else {
            float multiplier = connection.settings.ShrapnelDamagePercent / 1000.0f;

            damage = (connection.settings.BulletDamageLevel / 1000) +
                     (connection.settings.BulletDamageUpgrade / 1000) * weapon.data.level;

            damage = (int)(damage * multiplier);
          }
        } else {
          damage = (connection.settings.BulletDamageLevel / 1000) +
                   (connection.settings.BulletDamageUpgrade / 1000) * weapon.data.level;
        }
      } break;
      case WeaponType::Thor:
      case WeaponType::Bomb:
      case WeaponType::ProximityBomb: {
        int bomb_dmg = connection.settings.BombDamageLevel;
        int level = weapon.data.level;

        if (weapon.data.type == WeaponType::Thor) {
          // Weapon level should always be 0 for thor in normal gameplay, I believe, but this is how it's done
          bomb_dmg = bomb_dmg + bomb_dmg * weapon.data.level * weapon.data.level;
          level = 3 + weapon.data.level;
        }

        bomb_dmg = bomb_dmg / 1000;

        if (weapon.flags & WEAPON_FLAG_EMP) {
          bomb_dmg = (int)(bomb_dmg * (connection.settings.EBombDamagePercent / 1000.0f));
        }

        damage = bomb_dmg;
      } break;
      case WeaponType::Burst: {
        damage = connection.settings.BurstDamageLevel;
      } break;
    }

    return damage;
  }

  Vector2f set_position;
  float seconds_lookahead;
  float radius;
  const char* position_key = nullptr;
  const char* output_key = nullptr;
};

}  // namespace behavior
}  // namespace zero
