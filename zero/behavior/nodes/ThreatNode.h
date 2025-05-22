#pragma once

#include <stdio.h>
#include <zero/InfluenceMap.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

#include <random>

// These nodes are pretty bad. They could be improved by projecting weapons through an influence map and finding a
// nearby tile on the map that is safe.

namespace zero {
namespace behavior {

struct IncomingDamageReport {
  Vector2f average_direction;
  Vector2f average_origin;
  float average_damage;
  u32 weapon_count;
};

struct DodgeIncomingDamage : public behavior::BehaviorNode {
  DodgeIncomingDamage(float damage_percent_threshold, float distance)
      : damage_percent_threshold(damage_percent_threshold), distance(distance) {}
  DodgeIncomingDamage(float damage_percent_threshold, const char* distance_key)
      : damage_percent_threshold(damage_percent_threshold), distance_key(distance_key) {}
  DodgeIncomingDamage(const char* damage_percent_threshold_key, const char* distance_key)
      : damage_percent_threshold_key(damage_percent_threshold_key), distance_key(distance_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();

    if (!self) return behavior::ExecuteResult::Failure;
    if (self->ship >= 8) return behavior::ExecuteResult::Failure;

    float check_distance = distance;
    if (distance_key) {
      auto opt_distance = ctx.blackboard.Value<float>(distance_key);
      if (!opt_distance) return behavior::ExecuteResult::Failure;

      check_distance = *opt_distance;
    }

    float damage_percent_threshold = this->damage_percent_threshold;
    if (damage_percent_threshold_key) {
      auto opt_threshold = ctx.blackboard.Value<float>(damage_percent_threshold_key);
      if (!opt_threshold) return behavior::ExecuteResult::Failure;

      damage_percent_threshold = *opt_threshold;
    }

    IncomingDamageReport report = GetIncomingDamage(ctx, self, check_distance);
    float est_damage = report.weapon_count * report.average_damage;
    float new_energy = self->energy - est_damage;
    float damage_percent = est_damage / (float)ctx.bot->game->ship_controller.ship.energy;

    Vector2f incoming_direction = Normalize(report.average_direction);
    Ray ray(report.average_origin, incoming_direction);
    Vector2f closest_hit = ray.GetClosestPosition(self->position);

    Vector2f side = Normalize(self->position - closest_hit);

    if (est_damage > 0) {
      Vector3f color = Vector3f(1, 1, 0);
      if (damage_percent >= damage_percent_threshold || new_energy <= 0) {
        color = Vector3f(0, 1, 0);
      }

      ctx.bot->game->line_renderer.PushLine(self->position, Vector3f(1, 1, 0), self->position + side * 5.0f, color);
      ctx.bot->game->line_renderer.Render(ctx.bot->game->camera);
    }

    float force = 10000.0f;
    auto result = behavior::ExecuteResult::Success;

    // If we won't die then we should let the rest of the behavior tree run and apply only a small amount of force.
    if (damage_percent < damage_percent_threshold && new_energy > 0) {
      force = 2.0f + damage_percent * 10.0f;
      result = behavior::ExecuteResult::Failure;
    }

    // We are going to die or taking too much damage. Make the force very strong.
    ctx.bot->bot_controller->steering.force = side * force;

    return result;
  }

  IncomingDamageReport GetIncomingDamage(behavior::ExecuteContext& ctx, Player* self, float check_distance) {
    float distance_sq = check_distance * check_distance;
    float ship_radius = ctx.bot->game->connection.settings.ShipSettings[self->ship].GetRadius();
    float bounds_extent = ship_radius * 2.0f;

    Rectangle self_bounds(self->position - Vector2f(bounds_extent, bounds_extent),
                          self->position + Vector2f(bounds_extent, bounds_extent));

    Vector2f average_direction;
    Vector2f average_origin;
    float average_damage = 0.0f;
    size_t incoming_count = 0;

    auto& weapon_man = ctx.bot->game->weapon_manager;
    for (size_t i = 0; i < weapon_man.weapon_count; ++i) {
      Weapon& weapon = weapon_man.weapons[i];

      if (weapon.frequency == self->frequency) continue;
      if (weapon.data.type == WeaponType::Repel || weapon.data.type == WeaponType::Decoy) continue;
      if (weapon.data.type == WeaponType::Burst && !(weapon.flags & WEAPON_FLAG_BURST_ACTIVE)) continue;
      if (weapon.position.DistanceSq(self->position) > distance_sq) continue;

      bool is_mine = (weapon.data.type == WeaponType::Bomb || weapon.data.type == WeaponType::ProximityBomb) &&
                     weapon.data.alternate;

      Vector2f relative_velocity = weapon.velocity - self->velocity;
      Vector2f direction = Normalize(relative_velocity);
      Rectangle check_bounds = self_bounds;

      if (weapon.data.type == WeaponType::Bomb) {
        // Grow by bomb size plus some extra pixels to be certain.
        check_bounds = check_bounds.Grow(6.0f / 16.0f);
      } else if (weapon.data.type == WeaponType::ProximityBomb) {
        float prox_radius =
            ((float)ctx.bot->game->connection.settings.ProximityDistance + (float)weapon.data.level) + (2.0f / 16.0f);

        check_bounds = check_bounds.Grow(prox_radius);
      }

      Rectangle view_bounds = check_bounds.Translate(-self->position);
      view_bounds = view_bounds.Translate(weapon.position);

      ctx.bot->game->line_renderer.PushRect(view_bounds, Vector3f(1, 0, 0));

      // The amount of ticks that we are uncertain of with weapon alive time.
      // This will cause it to attempt to dodge weapons that might be right outside of hitting range.
      constexpr u32 kSlopTicks = 30;

      float remaining_distance =
          weapon.velocity.Length() * (TICK_DIFF(MAKE_TICK(weapon.end_tick + kSlopTicks), GetCurrentTick()) / 100.0f);

      float dist = 0.0f;

      if (RayBoxIntersect(Ray(weapon.position, direction), check_bounds, &dist, nullptr)) {
        // Ignore weapons that will time out before reaching us.
        if (dist > remaining_distance && !is_mine) continue;

        // Reduce the amount of impact this weapon will have based on its distance away.
        float threat_percent = (check_distance - dist) / (check_distance * 0.7f);
        if (threat_percent > 1.0f) threat_percent = 1.0f;
        if (threat_percent < 0.0f) threat_percent = 0.0f;

        float damage = (float)GetEstimatedWeaponDamage(weapon, ctx.bot->game->connection) * threat_percent;
        Vector2f weighted_direction = direction * threat_percent;

        // Reduce the effect of this direction if the damage is lower than average.
        if (average_damage > 0) {
          weighted_direction *= damage / average_damage;
        }

        average_direction +=
            (weighted_direction + (float)incoming_count * average_direction) / ((float)incoming_count + 1);
        average_origin += (weapon.position + (float)incoming_count * average_origin) / ((float)incoming_count + 1);
        average_damage += (damage + (float)incoming_count * average_damage) / ((float)incoming_count + 1);
        ++incoming_count;
      }
    }

    ctx.bot->game->line_renderer.Render(ctx.bot->game->camera);

    IncomingDamageReport report;
    report.average_damage = average_damage;
    report.average_direction = average_direction;
    report.average_origin = average_origin;
    report.weapon_count = (u32)incoming_count;

    return report;
  }

  float damage_percent_threshold = 0.0f;
  float distance = 0.0f;
  const char* distance_key = nullptr;
  const char* damage_percent_threshold_key = nullptr;
};

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

struct InfluenceMapPopulateEnemies : public BehaviorNode {
  InfluenceMapPopulateEnemies(float radius, float value, bool clear) : radius(radius), value(value), clear(clear) {}
  InfluenceMapPopulateEnemies(const char* radius_key, float value, bool clear)
      : radius_key(radius_key), value(value), clear(clear) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto& player_manager = ctx.bot->game->player_manager;
    Player* self = player_manager.GetSelf();
    if (!self || self->ship >= 8) return ExecuteResult::Failure;

    float influence_radius = radius;

    if (radius_key) {
      auto opt_radius = ctx.blackboard.Value<float>(radius_key);
      if (!opt_radius) return ExecuteResult::Failure;
      influence_radius = *opt_radius;
    }

    auto& influence_map = ctx.bot->bot_controller->influence_map;

    if (clear) {
      influence_map.Clear();
    }

    u32 current_tick = GetCurrentTick();

    for (size_t i = 0; i < player_manager.player_count; ++i) {
      Player* p = player_manager.players + i;

      if (p->ship >= 8) continue;
      if (p->frequency == self->frequency) continue;
      if (!player_manager.IsSynchronized(*p, current_tick)) continue;

      for (float y = p->position.y - influence_radius; y < p->position.y + influence_radius; ++y) {
        for (float x = p->position.x - influence_radius; x < p->position.x + influence_radius; ++x) {
          if (x >= 0 && y >= 0 && x <= 1023 && y <= 1023) {
            influence_map.AddValue((u16)x, (u16)y, value);
          }
        }
      }
    }

    return ExecuteResult::Success;
  }

  bool clear = false;
  float value = 1.0f;
  float radius = 1.0f;

  const char* radius_key = nullptr;
};

struct InfluenceMapPopulateWeapons : public BehaviorNode {
  InfluenceMapPopulateWeapons() {}
  InfluenceMapPopulateWeapons(bool clear) : clear(clear) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto& weapon_manager = ctx.bot->game->weapon_manager;
    auto& player_manager = ctx.bot->game->player_manager;
    Player* self = player_manager.GetSelf();

    auto& influence_map = ctx.bot->bot_controller->influence_map;

    if (clear) {
      influence_map.Clear();
    }

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

  bool clear = true;
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

    if (ctx.bot->game->GetMap().GetTileId(end_position) != kTileIdSafe) {
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
