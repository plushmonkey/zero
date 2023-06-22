#pragma once

#include <zero/game/Game.h>

namespace zero {

// Converts a steering force into actual key presses
struct Actuator {
  void Update(Game& game, InputState& input, const Vector2f& heading, const Vector2f& force, float rotation) {
    float enter_delay = (game.connection.settings.EnterDelay / 100.0f);
    Player* self = game.player_manager.GetSelf();

    if (!self || self->ship == 8) return;
    if (self->enter_delay > 0.0f && self->enter_delay < enter_delay) return;

    Vector2f steering_direction = heading;

    bool has_force = force.LengthSq() > 0.0f;
    if (has_force) {
      steering_direction = Normalize(force);
    }

    Vector2f rotate_target = steering_direction;

    if (rotation != 0.0f) {
      rotate_target = Rotate(self->GetHeading(), -rotation);
    }

    if (!has_force) {
      steering_direction = rotate_target;
    }

    Vector2f perp = Perpendicular(heading);
    bool behind = force.Dot(heading) < 0;
    bool leftside = steering_direction.Dot(perp) < 0;

    if (steering_direction.Dot(rotate_target) < 0.75) {
      float rotation = 0.1f;
      int sign = leftside ? 1 : -1;

      if (behind) sign *= -1;

      steering_direction = Rotate(rotate_target, rotation * sign);

      leftside = steering_direction.Dot(perp) < 0;
    }

    bool clockwise = !leftside;

    if (has_force) {
      if (behind) {
        input.SetAction(InputAction::Backward, true);
      } else {
        input.SetAction(InputAction::Forward, true);
      }
    }

    if (heading.Dot(steering_direction) < 0.996f) {
      input.SetAction(InputAction::Right, clockwise);
      input.SetAction(InputAction::Left, !clockwise);
    }
  }
};

}  // namespace zero
