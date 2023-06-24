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
    Vector2f aimshot = CalculateShot(self->position, target->position, self->velocity, target->velocity, weapon_speed);

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
