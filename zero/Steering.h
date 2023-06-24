#pragma once

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
    Vector2f heading = Rotate(self->GetHeading(), rotation);

    float rotation = atan2f(heading.y, heading.x) - atan2f(to_target.y, to_target.x);

    this->rotation += WrapToPi(rotation);
  }

  void Seek(Game& game, const Vector2f& target) {
    Player* self = game.player_manager.GetSelf();

    force += target - self->position;
  }

  void Seek(Game& game, const Vector2f& target, float target_distance) {
    Player* self = game.player_manager.GetSelf();

    Vector2f to_target = target - self->position;

    if (to_target.LengthSq() <= target_distance * target_distance) {
      return Seek(game, target - (Normalize(to_target) * target_distance));
    }

    force += target - self->position;
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
#if 0

    float weapon_speed = game.connection.settings.ShipSettings[self->ship].BulletSpeed / 16.0f / 10.0f;

    Vector2f to_target = target - self->position;

    if (to_target.LengthSq() <= target_distance * target_distance) {
      return Seek(game, target - (Normalize(to_target) * target_distance));
    }

    float away_speed = target_velocity.Dot(Normalize(to_target));
    float combined_speed = weapon_speed + away_speed;
    float time_to_target = 0.0f;

    if (combined_speed != 0.0f) {
      time_to_target = to_target.Length() / combined_speed;
    }

    if (time_to_target < 0.0f || time_to_target > 5.0f) {
      time_to_target = 0.0f;
    }

    Vector2f projected_pos = target + target_velocity * time_to_target;

    to_target = Normalize(projected_pos - self->position);

    if (to_target.Dot(Normalize(target - self->position)) < 0.0f) {
      projected_pos = target;
    }

    force += projected_pos - self->position;
#endif
  }

  void AvoidWalls(Game& game, float max_look_ahead) {
    constexpr float kDegToRad = 3.14159f / 180.0f;
    constexpr size_t kFeelerCount = 29;

    static_assert(kFeelerCount & 1, "Feeler count must be odd");

    Vector2f feelers[kFeelerCount];

    auto self = game.player_manager.GetSelf();
    if (!self) return;

    feelers[0] = Normalize(self->velocity);

    for (size_t i = 1; i < kFeelerCount; i += 2) {
      feelers[i] = Rotate(feelers[0], kDegToRad * (90.0f / kFeelerCount) * i);
      feelers[i + 1] = Rotate(feelers[0], -kDegToRad * (90.0f / kFeelerCount) * i);
    }

    float speed = self->velocity.Length();
    float max_speed = game.ship_controller.ship.speed / 16.0f / 10.0f;
    float look_ahead = max_look_ahead * (speed / max_speed);

    size_t force_count = 0;
    Vector2f force_acc;

    for (size_t i = 0; i < kFeelerCount; ++i) {
      float intensity = feelers[i].Dot(Normalize(self->velocity));
      float check_distance = look_ahead * intensity;
      Vector2f check = feelers[i] * intensity;
      CastResult result = game.GetMap().Cast(self->position, Normalize(feelers[i]), check_distance, self->frequency);

      if (result.hit) {
        float multiplier = ((check_distance - result.distance) / check_distance) * 1.5f;

        force_acc += Normalize(feelers[i]) * -Normalize(feelers[i]).Dot(feelers[0]) * multiplier * max_speed;

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
