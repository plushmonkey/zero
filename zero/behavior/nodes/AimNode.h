#pragma once

#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

#include <optional>

namespace zero {
namespace behavior {

inline std::optional<Vector2f> CalculateShot(const Vector2f& pShooter, const Vector2f& pTarget,
                                             const Vector2f& vShooter, const Vector2f& vTarget, float sProjectile) {
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
  } else {
    return std::nullopt;
  }

  solution = pTarget + (v * t);

  return std::optional(solution);
}

inline float GetWeaponSpeed(Game& game, Player& player, WeaponType type) {
  float weapon_speed = 0.0f;

  switch (type) {
    case WeaponType::Bomb:
    case WeaponType::ProximityBomb:
    case WeaponType::Thor: {
      weapon_speed = game.connection.settings.ShipSettings[player.ship].BombSpeed / 16.0f / 10.0f;
    } break;
    case WeaponType::Bullet:
    case WeaponType::BouncingBullet: {
      weapon_speed = game.connection.settings.ShipSettings[player.ship].BulletSpeed / 16.0f / 10.0f;
    } break;
    case WeaponType::Decoy: {
      // Do nothing since it just matches the player's velocity.
    } break;
    default: {
    } break;
  }

  return weapon_speed;
}

struct BulletDistanceNode : public BehaviorNode {
  BulletDistanceNode(const char* output_key) : output_key(output_key) {}
  BulletDistanceNode(const char* player_key, const char* output_key) : player_key(player_key), output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto player = ctx.bot->game->player_manager.GetSelf();
    if (player_key) {
      auto opt_player = ctx.blackboard.Value<Player*>(player_key);
      if (!opt_player) return ExecuteResult::Failure;
      player = *opt_player;
    }

    if (!player) return ExecuteResult::Failure;
    if (player->ship >= 8) return ExecuteResult::Failure;

    float weapon_speed = GetWeaponSpeed(*ctx.bot->game, *player, WeaponType::Bullet);
    float alive_time = ctx.bot->game->connection.settings.BulletAliveTime / 100.0f;

    Vector2f velocity = player->velocity + player->GetHeading() * weapon_speed;
    Vector2f trajectory = velocity * alive_time;

    ctx.blackboard.Set<float>(output_key, trajectory.Length());

    return ExecuteResult::Success;
  }

  const char* player_key = nullptr;
  const char* output_key = nullptr;
};

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

    float weapon_speed = GetWeaponSpeed(*ctx.bot->game, *player, weapon_type);
    Vector2f velocity = player->velocity + player->GetHeading() * weapon_speed;

    ctx.blackboard.Set(velocity_key, velocity);

    return ExecuteResult::Success;
  }

  const char* player_key = nullptr;
  const char* velocity_key = nullptr;
  WeaponType weapon_type;
};

struct AimNode : public BehaviorNode {
  AimNode(WeaponType weapon_type, const char* target_player_key, const char* position_key)
      : weapon_type(weapon_type), target_player_key(target_player_key), position_key(position_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return ExecuteResult::Failure;

    auto opt_target = ctx.blackboard.Value<Player*>(target_player_key);
    if (!opt_target.has_value()) return ExecuteResult::Failure;

    Player* target = opt_target.value();
    if (!target) return ExecuteResult::Failure;

    float weapon_speed = GetWeaponSpeed(*ctx.bot->game, *self, weapon_type);
    Vector2f weapon_velocity = self->velocity + self->GetHeading() * weapon_speed;

    Vector2f direction = Normalize(target->position - self->position);
    float away_amount = target->velocity.Dot(direction);

    Vector2f target_velocity = target->velocity;
    // If the enemy is moving away too fast, ignore the away movement for this calculation.
    if (away_amount > weapon_speed) {
      // Remove the "away" velocity from the target so it's only moving side to side.
      target_velocity = target->velocity - direction * away_amount;
    }

    std::optional<Vector2f> calculated_shot =
        CalculateShot(self->position, target->position, self->velocity, target_velocity, weapon_velocity.Length());

    // Default to target's position if the calculated shot fails.
    Vector2f aimshot = target->position;

    if (calculated_shot.has_value()) {
      aimshot = *calculated_shot;

      // Set the aimshot directly to the player position if it is too far away.
      if (aimshot.DistanceSq(target->position) > 20.0f * 20.0f) {
        aimshot = target->position;
      }
    }

    ctx.blackboard.Set(position_key, aimshot);

    return ExecuteResult::Success;
  }

  WeaponType weapon_type;
  const char* target_player_key;
  const char* position_key;
};

}  // namespace behavior
}  // namespace zero
