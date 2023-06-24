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
};

}  // namespace zero
