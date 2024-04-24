#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/Behavior.h>
#include <zero/behavior/nodes/AimNode.h>
#include <zero/zones/devastation/Devastation.h>

namespace zero {
namespace deva {

struct Particle {
  Vector2f position;
  Vector2f velocity;
  int frequency;
  float alive_time;
  int remaining_bounces;

  Particle(Vector2f position, Vector2f velocity, int frequency, float alive_time, int bounces)
      : position(position),
        velocity(velocity),
        frequency(frequency),
        alive_time(alive_time),
        remaining_bounces(bounces) {}
};

struct ParticleSimulator {
  Map& map;

  ParticleSimulator(Map& map) : map(map) {}

  // Returns false if particle sim is finished.
  bool Simulate(Particle& particle) const {
    const float dt = 1.0f / 100.0f;

    bool x_collide = SimulateAxis(particle, 0);
    bool y_collide = SimulateAxis(particle, 1);

    if (x_collide || y_collide) {
      if (--particle.remaining_bounces < 0) {
        return false;
      }
    }

    particle.alive_time -= dt;

    return particle.alive_time > 0.0f;
  }

  // Returns true if weapon bounced.
  inline bool SimulateAxis(Particle& particle, int axis) const {
    const float dt = 1.0f / 100.0f;

    float previous = particle.position[axis];

    particle.position[axis] += particle.velocity[axis] * dt;

    // TODO: Handle other special tiles here
    if (map.IsSolid((u16)floorf(particle.position.x), (u16)floorf(particle.position.y), particle.frequency)) {
      particle.position[axis] = previous;
      particle.velocity[axis] = -particle.velocity[axis];

      return true;
    }

    return false;
  }
};

// Returns success if aiming in a way that will have the weapon shot collide with the enemy.
struct BounceShotQueryNode : public behavior::BehaviorNode {
  BounceShotQueryNode(WeaponType weapon_type, const char* enemy_player_key, float radius_multiplier = 1.0f)
      : weapon_type(weapon_type), enemy_player_key(enemy_player_key), radius_multiplier(radius_multiplier) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto opt_enemy = ctx.blackboard.Value<Player*>(enemy_player_key);
    if (!opt_enemy) return behavior::ExecuteResult::Failure;

    Player* enemy = *opt_enemy;
    if (!enemy || enemy->ship >= 8) return behavior::ExecuteResult::Failure;

    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto& settings = ctx.bot->game->connection.settings;

    int bounces = 0;
    float weapon_speed = behavior::GetWeaponSpeed(*ctx.bot->game, *self, weapon_type);
    float alive_time = ctx.bot->game->weapon_manager.GetWeaponTotalAliveTime(weapon_type, false) / 100.0f;

    switch (weapon_type) {
      case WeaponType::Burst:
      case WeaponType::BouncingBullet: {
        bounces = 0xFFFF;
      } break;
      case WeaponType::Bomb:
      case WeaponType::ProximityBomb: {
        bounces = settings.ShipSettings[self->ship].BombBounceCount;
      } break;
      default: {
      } break;
    }

    // Stop it from projecting very far into the future.
    if (alive_time > 5.0f) alive_time = 5.0f;

    float enemy_radius = settings.ShipSettings[enemy->ship].GetRadius() * radius_multiplier;
    Rectangle collider = Rectangle::FromPositionRadius(enemy->position, enemy_radius);

    Vector2f velocity = self->velocity + self->GetHeading() * weapon_speed;

    ParticleSimulator sim(ctx.bot->game->GetMap());

    if ((weapon_type == WeaponType::Bullet || weapon_type == WeaponType::BouncingBullet) &&
        settings.ShipSettings[self->ship].DoubleBarrel) {
      Vector2f heading = self->GetHeading();
      Vector2f perp = Perpendicular(heading);
      Vector2f offset = perp * (settings.ShipSettings[self->ship].GetRadius() * 0.75f);

      Particle weapon1(self->position - offset, velocity, self->frequency, alive_time, bounces);
      if (SimulateWeapon(sim, weapon1, collider)) {
        return behavior::ExecuteResult::Success;
      }

      Particle weapon2(self->position + offset, velocity, self->frequency, alive_time, bounces);
      if (SimulateWeapon(sim, weapon2, collider)) {
        return behavior::ExecuteResult::Success;
      }
    } else {
      Particle weapon(self->position, velocity, self->frequency, alive_time, bounces);

      if (SimulateWeapon(sim, weapon, collider)) {
        return behavior::ExecuteResult::Success;
      }
    }

    return behavior::ExecuteResult::Failure;
  }

  // Returns true if the weapon collided
  bool SimulateWeapon(ParticleSimulator sim, Particle particle, const Rectangle& collider) {
    // Simulate the weapon through the map and check if it overlaps the enemy's collider every tick.
    while (sim.Simulate(particle)) {
      if (collider.ContainsInclusive(particle.position)) {
        return true;
      }
    }

    return false;
  }

  const char* enemy_player_key = nullptr;
  float radius_multiplier = 1.0f;
  WeaponType weapon_type = WeaponType::BouncingBullet;
};

}  // namespace deva
}  // namespace zero
