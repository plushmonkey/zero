#pragma once

#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

inline Vector2f CalculateShot(const Vector2f& pShooter, const Vector2f& pTarget, const Vector2f& vShooter,
                              const Vector2f& vTarget, float sProjectile) {
  Vector2f totarget = pTarget - pShooter;
  Vector2f v = vTarget - vShooter;

  float a = v.Dot(v) - sProjectile * sProjectile;
  float b = 2 * v.Dot(totarget);
  float c = totarget.Dot(totarget);

  Vector2f solution;

  float disc = (b * b) - 4 * a * c;
  float t = -1.0;

  if (disc >= 0.0) {
    float t1 = (-b + sqrtf(disc)) / (2 * a);
    float t2 = (-b - sqrtf(disc)) / (2 * a);
    if (t1 < t2 && t1 >= 0)
      t = t1;
    else
      t = t2;
  }

  solution = pTarget + (v * t);

  return solution;
}

struct ShotVelocityQueryNode : public BehaviorNode {
  ShotVelocityQueryNode(WeaponType weapon, const char* velocity_key)
      : weapon_type(weapon), player_key(nullptr), velocity_key(velocity_key) {}
  ShotVelocityQueryNode(const char* player_key, WeaponType weapon, const char* velocity_key)
      : weapon_type(weapon), player_key(player_key), velocity_key(velocity_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    Player* player = nullptr;

    if (player_key) {
      auto opt_player = ctx.blackboard.Value<Player*>(player_key);
      if (!opt_player.has_value()) return ExecuteResult::Failure;

      player = opt_player.value();
    } else {
      player = ctx.bot->game->player_manager.GetSelf();
    }

    if (!player) return ExecuteResult::Failure;
    if (player->ship >= 8) return ExecuteResult::Failure;

    Vector2f velocity;
    float weapon_speed = 0.0f;

    switch (weapon_type) {
      case WeaponType::Bomb:
      case WeaponType::ProximityBomb:
      case WeaponType::Thor: {
        weapon_speed = ctx.bot->game->connection.settings.ShipSettings[player->ship].BombSpeed / 16.0f / 10.0f;
      } break;
      case WeaponType::Bullet:
      case WeaponType::BouncingBullet: {
        weapon_speed = ctx.bot->game->connection.settings.ShipSettings[player->ship].BulletSpeed / 16.0f / 10.0f;
      } break;
      case WeaponType::Decoy: {
        // Do nothing since it just matches the player's velocity.
      } break;
      default: {
        return ExecuteResult::Failure;
      } break;
    }

    velocity = player->velocity + player->GetHeading() * weapon_speed;

    ctx.blackboard.Set(velocity_key, velocity);

    return ExecuteResult::Success;
  }

  const char* player_key;
  const char* velocity_key;
  WeaponType weapon_type;
};

struct AimNode : public BehaviorNode {
  AimNode(const char* target_player_key, const char* position_key)
      : target_player_key(target_player_key), position_key(position_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return ExecuteResult::Failure;

    auto opt_target = ctx.blackboard.Value<Player*>(target_player_key);
    if (!opt_target.has_value()) return ExecuteResult::Failure;

    float weapon_speed = ctx.bot->game->connection.settings.ShipSettings[self->ship].BulletSpeed / 16.0f / 10.0f;

    Player* target = opt_target.value();

    Vector2f direction = Normalize(target->position - self->position);
    float amount = target->velocity.Dot(direction);
    // Remove the "away" velocity from the target so it's only moving side to side.
    Vector2f horizontal_vel = target->velocity - direction * amount;

    Vector2f aimshot = CalculateShot(self->position, target->position, self->velocity, horizontal_vel, weapon_speed);

    // Set the aimshot directly to the player position if the calculated position was way off.
    if (aimshot.DistanceSq(target->position) > 20.0f * 20.0f) {
      aimshot = target->position;
    }

    ctx.blackboard.Set(position_key, aimshot);

    return ExecuteResult::Success;
  }

  const char* target_player_key;
  const char* position_key;
};

}  // namespace behavior
}  // namespace zero
