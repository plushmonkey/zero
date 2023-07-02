#pragma once

#include <math.h>
#include <zero/Math.h>
#include <zero/game/Game.h>

namespace zero {

struct Steering {
  Vector2f force;
  float rotation = 0.0f;

  void Reset() {
    force = Vector2f(0, 0);
    rotation = 0.0f;
  }

  void Face(Game& game, const Vector2f& target) {
    Player* self = game.player_manager.GetSelf();

    Vector2f to_target = target - self->position;
    Vector2f heading = Rotate(self->GetHeading(), -rotation);

    float rotation = atan2f(heading.y, heading.x) - atan2f(to_target.y, to_target.x);

    this->rotation += WrapToPi(rotation);
  }

  void SeekZero(Game& game) {
    Player* self = game.player_manager.GetSelf();

    Vector2f desired_velocity(0, 0);

    force += desired_velocity - self->velocity;
  }

  void Seek(Game& game, const Vector2f& target) {
    Player* self = game.player_manager.GetSelf();

    float max_speed = game.player_manager.ship_controller->ship.speed / 16.0f / 10.0f;

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

  void Arrive(Game& game, const Vector2f& target, float deceleration) {
    Player* self = game.player_manager.GetSelf();

    Vector2f to_target = target - self->position;
    float distance = to_target.Length();
    float max_speed = game.ship_controller.ship.speed / 16.0f / 10.0f;

    if (distance > 0) {
      float speed = distance / deceleration;

      speed = fminf(speed, max_speed);

      Vector2f desired = to_target * (speed / distance);

      force += desired - self->velocity;
    }
  }

  void Pursue(Game& game, const Vector2f& target_position, const Player& target, float target_distance) {
    Player* self = game.player_manager.GetSelf();
    float max_speed = game.ship_controller.ship.speed / 16.0f / 10.0f;
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

    float max_speed = game.ship_controller.ship.speed / 16.0f / 10.0f;
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
};

}  // namespace zero
