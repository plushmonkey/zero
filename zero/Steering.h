#pragma once

#include <math.h>
#include <zero/Math.h>
#include <zero/game/Game.h>
#include <zero/game/Logger.h>

namespace zero {

struct Steering {
  Vector2f force;
  float rotation = 0.0f;
  float rotation_threshold = 0.75f;

  void Reset() {
    force = Vector2f(0, 0);
    rotation = 0.0f;
    rotation_threshold = 0.75f;
  }

  inline static float GetMaxSpeed(Game& game) {
    Player* self = game.player_manager.GetSelf();
    if (!self) return 0.0f;

    u32 int_speed = game.ship_controller.ship.speed;

    if (game.ship_controller.ship.gravity_effect) {
      u32 gravity_speed = (u32)game.connection.settings.ShipSettings[self->ship].GravityTopSpeed;

      if (gravity_speed > int_speed) {
        int_speed = gravity_speed;
      }
    }

    if (game.ship_controller.ship.last_speed > int_speed) {
      int_speed = game.ship_controller.ship.last_speed;
    }

    return int_speed / 16.0f / 10.0f;
  }

  void Face(Game& game, const Vector2f& target) {
    Player* self = game.player_manager.GetSelf();

    Vector2f to_target = target - self->position;
    Vector2f heading = Rotate(self->GetHeading(), -rotation);

    float rotation = atan2f(heading.y, heading.x) - atan2f(to_target.y, to_target.x);

    this->rotation += WrapToPi(rotation);
  }

  inline void SetRotationThreshold(float threshold) { this->rotation_threshold = threshold; }

  void SeekZero(Game& game) {
    Player* self = game.player_manager.GetSelf();

    Vector2f desired_velocity(0, 0);

    force += desired_velocity - self->velocity;
  }

  void Seek(Game& game, const Vector2f& target) {
    Player* self = game.player_manager.GetSelf();

    float max_speed = GetMaxSpeed(game);

    Vector2f desired_velocity = Normalize(target - self->position) * max_speed;

    force += desired_velocity - self->velocity;
  }

  void Seek(Game& game, const Vector2f& target, float target_distance) {
    Player* self = game.player_manager.GetSelf();

    Vector2f to_target = target - self->position;

    if (to_target.LengthSq() <= target_distance * target_distance) {
      return Seek(game, target - (Normalize(to_target) * target_distance));
    }

    Seek(game, target);
  }

  void Arrive(Game& game, const Vector2f& target, float slow_radius, float min_multiplier = 0.0f) {
    Player* self = game.player_manager.GetSelf();

    Vector2f to_target = target - self->position;
    float distance = to_target.Length();
    float max_speed = GetMaxSpeed(game);

    if (distance > 0) {
      float multiplier = 1.0f;

      if (distance < slow_radius) {
        multiplier = distance / slow_radius;
        if (multiplier < min_multiplier) multiplier = min_multiplier;
      }

      Vector2f desired = Normalize(to_target) * max_speed * multiplier;

      force += desired - self->velocity;
    }
  }

  void Pursue(Game& game, const Vector2f& target_position, const Player& target, float target_distance) {
    Player* self = game.player_manager.GetSelf();
    float max_speed = GetMaxSpeed(game);
    Vector2f to_target = target_position - self->position;
    float t = to_target.Length() / (max_speed + target.velocity.Length());

    if (to_target.LengthSq() <= target_distance * target_distance) {
      return Seek(game, target_position - (Normalize(to_target) * target_distance));
    }

    float dot = self->GetHeading().Dot(target.GetHeading());

    if (to_target.Dot(self->GetHeading()) > 0 && dot < -0.95f) {
      return Seek(game, target_position);
    }

    return Seek(game, target_position + target.velocity * t);
  }

  void AvoidWalls(Game& game) {
    constexpr float kDegToRad = 3.14159f / 180.0f;
    constexpr size_t kFeelerCount = 29;

    static_assert(kFeelerCount & 1, "Feeler count must be odd");

    Vector2f feelers[kFeelerCount];

    auto self = game.player_manager.GetSelf();
    if (!self || self->ship == 8) return;

    feelers[0] = Normalize(self->velocity);

    if (self->velocity.LengthSq() < 0.5f * 0.5f) {
      feelers[0] = self->GetHeading();
    }

    for (size_t i = 1; i < kFeelerCount; i += 2) {
      feelers[i] = Rotate(feelers[0], kDegToRad * (90.0f / kFeelerCount) * i);
      feelers[i + 1] = Rotate(feelers[0], -kDegToRad * (90.0f / kFeelerCount) * i);
    }

    float max_speed = GetMaxSpeed(game);
    float thrust = (game.ship_controller.ship.thrust * (10.0f / 16.0f));
    // Seconds to go to zero from max
    float max_ttz = max_speed / thrust;
    // Time it takes to go to zero times max speed is the farthest look-ahead needed.
    float look_ahead = max_ttz * max_speed;

    float radius = game.connection.settings.ShipSettings[self->ship].GetRadius();

    size_t force_count = 0;
    Vector2f force_acc;

    for (size_t i = 0; i < kFeelerCount; ++i) {
      constexpr float kMinFeelerDistance = 2.0f;
      float intensity = feelers[i].Dot(feelers[0]);
      float check_distance = max(look_ahead * intensity, kMinFeelerDistance);
      Vector2f direction = Normalize(feelers[i]);
      Vector2f start = self->position + direction * radius;

      CastResult result = game.GetMap().Cast(start, direction, check_distance, self->frequency);

      if (result.hit) {
        float multiplier = ((check_distance - result.distance) / check_distance) * max_speed;

        force_acc += direction * -direction.Dot(feelers[0]) * multiplier;

        ++force_count;
      } else {
        result.distance = check_distance;
      }
    }

    if (force_count > 0) {
      this->force += force_acc * (1.0f / force_count);
    }
  }

  // Repel self from teammate to avoid synchronized movements
  void AvoidTeam(Game& game, float dist) {
    auto& pm = game.player_manager;

    auto self = pm.GetSelf();
    if (!self || dist <= 0.0f) return;

    Vector2f avoid_force;
    float count = 0.0f;

    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* player = pm.players + i;

      if (player->frequency != self->frequency) continue;
      if (player->id == self->id) continue;
      if (player->IsRespawning()) continue;
      if (player->position == Vector2f(0, 0)) continue;
      if (!game.player_manager.IsSynchronized(*player)) continue;

      float dist_sq = player->position.DistanceSq(self->position);
      if (dist_sq > dist * dist) continue;

      float team_dist = sqrtf(dist_sq);
      float diff = dist - team_dist;

      avoid_force += Normalize(self->position - player->position) * (diff * diff);
      ++count;
    }

    if (count > 0) {
      this->force += (avoid_force / count);
    }

  }
};

}  // namespace zero
