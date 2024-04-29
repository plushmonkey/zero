#include "Actuator.h"

namespace zero {

void Actuator::Update(Game& game, InputState& input, const Vector2f& force, float rotation, float rotation_threshold) {
  float enter_delay = (game.connection.settings.EnterDelay / 100.0f);
  Player* self = game.player_manager.GetSelf();

  // Make sure we are in a ship and not dead.
  if (!self || self->ship == 8) return;
  if (self->enter_delay > 0.0f && self->enter_delay < enter_delay) return;

  Vector2f heading = self->GetHeading();

  // Default the steering direction to the current heading, meaning no rotation needs to happen.
  Vector2f steering_direction = heading;

  bool has_force = force.LengthSq() > 0.0f;

  // If we have some steering force, set that as the target orientation.
  if (has_force) {
    steering_direction = Normalize(force);
  }

  Vector2f rotate_target = steering_direction;

  // Rotate from the heading by the rotation target amount.
  if (rotation != 0.0f) {
    rotate_target = Rotate(self->GetHeading(), -rotation);
  }

  // If there was no force, then hard set the target orientation as the rotation target.
  if (!has_force) {
    steering_direction = rotate_target;
  }

  Vector2f perp = Perpendicular(heading);
  bool behind = steering_direction.Dot(heading) < 0;
  bool leftside = steering_direction.Dot(perp) < 0;

  // If our target orientation is far from the rotate target, keep it close by rotating back toward it.
  // This is used as a way to rectify having a force and rotation target. Having both means that there must be some
  // blend of the two. It keeps the heading locked around the rotate target with some wiggle room for also aiming at the
  // steering direction.
  if (steering_direction.Dot(rotate_target) < rotation_threshold) {
    float rotation = 0.1f;
    int sign = leftside ? 1 : -1;

    // If the steering orientation target is behind us, then flip the rotation direction so it can reverse into it.
    if (behind) sign *= -1;

    // Hard set the steering target orientation to being within the rotate target orientation wiggle room.
    steering_direction = Rotate(rotate_target, rotation * sign);

    // Check again which direction we need to rotate to reach the steering orientation target.
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

}  // namespace zero
