#pragma once

#include <stdio.h>
#include <zero/InfluenceMap.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

#include <random>
#include <unordered_set>

// These nodes are pretty bad. They could be improved by projecting weapons through an influence map and finding a
// nearby tile on the map that is safe.

namespace zero {
namespace behavior {

struct InfluenceMapGradientDodge : public BehaviorNode {
  ExecuteResult Execute(ExecuteContext& ctx) override {
    bool dodged = Dodge(ctx);

    return dodged ? ExecuteResult::Success : ExecuteResult::Failure;
  }

  bool Dodge(behavior::ExecuteContext& ctx) {
    Game& game = *ctx.bot->game;
    Player* self = game.player_manager.GetSelf();

    if (!self || self->ship >= 8) return false;

    float radius = game.connection.settings.ShipSettings[self->ship].GetRadius();
    float energy_pct = self->energy / (float)game.ship_controller.ship.energy;

    size_t search = 3;

    if (energy_pct < 0.25f) {
      search += 2;
    } else if (energy_pct < 0.5f) {
      search += 1;
    }

    Vector2f pos = self->position;
    // Check the corners of the ship to see if it's touching any influenced tiles
    Vector2f positions[] = {
        pos,
        pos + Vector2f(radius, radius),
        pos + Vector2f(-radius, -radius),
        pos + Vector2f(radius, -radius),
        pos + Vector2f(-radius, radius),
    };

    float best_value = 1000000.0f;
    Vector2f best_position;

    for (size_t i = 0; i < sizeof(positions) / sizeof(*positions); ++i) {
      Vector2f check = positions[i];

      if (ctx.bot->bot_controller->influence_map.GetValue(check) > 0.0f) {
        Vector2f offsets[] = {
            Vector2f(0, -1), Vector2f(0, 1),  Vector2f(-1, 0), Vector2f(1, 0),
            Vector2f(1, 1),  Vector2f(1, -1), Vector2f(-1, 1), Vector2f(-1, -1),
        };

        for (size_t i = 0; i < sizeof(offsets) / sizeof(*offsets); ++i) {
          float value = 0.0f;
#if 0
          float heading_value = std::abs(Normalize(offsets[i]).Dot(Normalize(self->GetHeading())));
#else
          float heading_value = 1.0f;
#endif

          for (size_t j = 1; j <= search; ++j) {
            if (!game.GetMap().CanOccupy(check + offsets[i] * (float)j, radius, 0xFFFF)) {
              value += 10000.0f;
            }
            value += ctx.bot->bot_controller->influence_map.GetValue(check + offsets[i] * (float)j) * heading_value;
          }

          if (value < best_value && game.GetMap().CanOccupy(check + offsets[i], radius, 0xFFFF)) {
            best_value = value;
            best_position = check + offsets[i];
          }
        }
      }
    }

    if (best_value < 1000000.0f) {
      ctx.bot->bot_controller->steering.force = (best_position - self->position) * 10000.0f;
      return true;
    }

    return false;
  }
};

struct InfluenceMapPopulateWeapons : public BehaviorNode {
  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto& weapon_manager = ctx.bot->game->weapon_manager;
    auto& player_manager = ctx.bot->game->player_manager;
    Player* self = player_manager.GetSelf();

    auto& influence_map = ctx.bot->bot_controller->influence_map;

    influence_map.Clear();

    if (!self) return ExecuteResult::Failure;

    u32 tick = GetCurrentTick();

    for (size_t i = 0; i < weapon_manager.weapon_count; ++i) {
      Weapon* weapon = weapon_manager.weapons + i;

      Player* player = player_manager.GetPlayerById(weapon->player_id);

      if (!player || player->frequency == self->frequency) continue;

      float dmg = (float)GetEstimatedWeaponDamage(*weapon, ctx.bot->game->connection);
      Vector2f direction = Normalize(weapon->velocity);

      constexpr s32 kForwardThinkingTicks = 500;

      s32 remaining_ticks = TICK_DIFF(weapon->end_tick, tick);
      if (remaining_ticks > kForwardThinkingTicks) remaining_ticks = kForwardThinkingTicks;

      float speed = weapon->velocity.Length();
      float remaining_dist = (remaining_ticks / 100.0f) * speed;

      CastInfluence(influence_map, ctx.bot->game->connection.map, weapon->position, direction, remaining_dist, dmg);

      if (weapon->data.type == WeaponType::Bomb || weapon->data.type == WeaponType::ProximityBomb) {
        Vector2f side = Normalize(Perpendicular(weapon->velocity));

        for (int i = 1; i <= 2; ++i) {
          CastInfluence(influence_map, ctx.bot->game->connection.map, weapon->position + side * (float)i, direction,
                        remaining_dist, dmg);
          CastInfluence(influence_map, ctx.bot->game->connection.map, weapon->position - side * (float)i, direction,
                        remaining_dist, dmg);
        }
      }
    }

    return ExecuteResult::Success;
  }

  void CastInfluence(InfluenceMap& influence_map, const Map& map, const Vector2f& from, const Vector2f& direction,
                     float max_length, float value) {
    Vector2f vMapSize = {1024.0f, 1024.0f};

    if (map.IsSolid(from, 0xFFFF)) {
      return;
    }

    Vector2f vRayUnitStepSize(std::sqrt(1 + (direction.y / direction.x) * (direction.y / direction.x)),
                              std::sqrt(1 + (direction.x / direction.y) * (direction.x / direction.y)));

    Vector2f vRayStart = from;

    Vector2f vMapCheck = Vector2f(std::floor(vRayStart.x), std::floor(vRayStart.y));
    Vector2f vRayLength1D;

    Vector2f vStep;

    if (direction.x < 0) {
      vStep.x = -1.0f;
      vRayLength1D.x = (vRayStart.x - float(vMapCheck.x)) * vRayUnitStepSize.x;
    } else {
      vStep.x = 1.0f;
      vRayLength1D.x = (float(vMapCheck.x + 1) - vRayStart.x) * vRayUnitStepSize.x;
    }

    if (direction.y < 0) {
      vStep.y = -1.0f;
      vRayLength1D.y = (vRayStart.y - float(vMapCheck.y)) * vRayUnitStepSize.y;
    } else {
      vStep.y = 1.0f;
      vRayLength1D.y = (float(vMapCheck.y + 1) - vRayStart.y) * vRayUnitStepSize.y;
    }

    // Perform "Walk" until collision or range check
    bool bTileFound = false;
    float fMaxDistance = max_length;
    float fDistance = 0.0f;

    while (!bTileFound && fDistance < fMaxDistance) {
      // Walk along shortest path
      if (vRayLength1D.x < vRayLength1D.y) {
        vMapCheck.x += vStep.x;
        fDistance = vRayLength1D.x;
        vRayLength1D.x += vRayUnitStepSize.x;
      } else {
        vMapCheck.y += vStep.y;
        fDistance = vRayLength1D.y;
        vRayLength1D.y += vRayUnitStepSize.y;
      }

      // Test tile at new test point
      if (vMapCheck.x >= 0 && vMapCheck.x < vMapSize.x && vMapCheck.y >= 0 && vMapCheck.y < vMapSize.y) {
        if (map.IsSolid((unsigned short)vMapCheck.x, (unsigned short)vMapCheck.y, 0xFFFF)) {
          bTileFound = true;
        } else {
          influence_map.AddValue((u16)vMapCheck.x, (u16)vMapCheck.y, value * (1.0f - (fDistance / fMaxDistance)));
        }
      }
    }
  }
};

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

    links.clear();

    for (size_t i = 0; i < weapon_manager.weapon_count; ++i) {
      Weapon& weapon = weapon_manager.weapons[i];

      if (weapon.frequency == freq) continue;
      if (weapon.data.type == WeaponType::Repel || weapon.data.type == WeaponType::Decoy) continue;
      if (weapon.data.type == WeaponType::Burst && !(weapon.flags & WEAPON_FLAG_BURST_ACTIVE)) continue;
      if (links.contains(weapon.link_id)) continue;

      if (weapon.link_id != kInvalidLink) {
        links.insert(weapon.link_id);
      }

      float dist = 0.0f;
      Vector2f start = weapon.position;

      float bounds_scale = weapon.data.type == WeaponType::ProximityBomb ? 2.0f : 1.0f;
      Rectangle check_bounds = bounds.Scale(bounds_scale);

      if (RayBoxIntersect(Ray(start, Normalize(weapon.velocity)), check_bounds, &dist, nullptr)) {
        Vector2f end = weapon.position + weapon.velocity * seconds_lookahead;

        if (start.DistanceSq(end) >= dist * dist) {
          int damage = GetEstimatedWeaponDamage(weapon, connection);
          energy -= damage;

          if (energy <= 0) break;
        }
      }
    }

    if (energy < 0) energy = 0;

    return (self->energy - energy) / ctx.bot->game->ship_controller.ship.energy;
  }

  Vector2f set_position;
  float seconds_lookahead;
  float radius;
  const char* position_key = nullptr;
  const char* output_key = nullptr;

  std::unordered_set<u32> links;
};

}  // namespace behavior
}  // namespace zero
