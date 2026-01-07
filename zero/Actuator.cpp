#include "Actuator.h"

namespace zero {

static inline float SignedAngle(const Vector2f& a, const Vector2f& b) {
  float cross = a.x * b.y - a.y * b.x;
  return atan2f(cross, a.Dot(b));
}

static float CalculateRotatedTravelTime(Game& game, const Player& self, const Vector2f& heading,
                                        const Vector2f& steering_direction, const Vector2f& force) {
  float seconds_per_rotation = 1.0f;

  if (game.ship_controller.ship.rotation > 0) {
    seconds_per_rotation = 400.0f / game.ship_controller.ship.rotation;
  }

  // Calculate the percentage of an entire ship rotation that is necessary to reach the requested steering direction.
  float rotate_percent = (1.0f - heading.Dot(steering_direction)) * 0.25f;

  // How long it takes to reach a rotation that would directly point at the steering direction.
  float seconds_to_rotate = rotate_percent * seconds_per_rotation;

  float distance = force.Length();
  float speed = self.velocity.Dot(steering_direction) * self.velocity.Length();
  float maxspeed = game.connection.settings.ShipSettings[self.ship].MaximumSpeed / 10.0f / 16.0f;

  float thrust = game.ship_controller.ship.thrust * (10.0f / 16.0f);

  // Assume we apply no thrust while rotating, so we adjust remaining distance by inertia speed.
  distance -= speed * seconds_to_rotate;

  float travel_seconds = 0.0f;

  while (distance > 0.0f) {
    speed = speed + thrust;
    if (speed > maxspeed) speed = maxspeed;

    if (speed > distance) {
      travel_seconds += distance / speed;
    } else {
      travel_seconds += 1.0f;
    }

    distance -= speed;
  }

  return travel_seconds;
}

void Actuator::Update(Game& game, InputState& input, const Vector2f& force, float rotation, float rotation_threshold) {
  if (!enabled) return;

  float enter_delay = (game.connection.settings.EnterDelay / 100.0f);
  Player* self = game.player_manager.GetSelf();

  // Make sure we are in a ship and not dead.
  if (!self || self->ship >= 8) return;
  if (self->enter_delay > 0.0f && self->enter_delay < enter_delay) return;

  Vector2f heading = self->GetHeading();

  // Default the steering direction to the current heading, meaning no rotation needs to happen.
  Vector2f steering_direction = heading;

  bool has_force = force.LengthSq() > 0.0f;
  bool has_rotation = rotation != 0.0f;

  // If we have some steering force, set that as the target orientation.
  if (has_force) {
    steering_direction = Normalize(force);
  }

  Vector2f rotate_target = steering_direction;

  // Rotate from the heading by the rotation target amount.
  if (has_rotation) {
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

  constexpr float kRequiredForwardAngle = 0.3f;

  // If our target is behind us, calculate how long it would take to rotate toward it.
  // If it takes too long, just go backwards there, otherwise rotate and go forward.
  if (behind && !has_rotation) {
    float forward_seconds = CalculateRotatedTravelTime(game, *self, heading, steering_direction, force);
    float reverse_seconds = CalculateRotatedTravelTime(game, *self, -heading, steering_direction, force);

    if (reverse_seconds < forward_seconds) {
      heading = -heading;

      bool clockwise = leftside;
      if (heading.Dot(steering_direction) < 0.996f) {
        input.SetAction(InputAction::Right, clockwise);
        input.SetAction(InputAction::Left, !clockwise);
      }

      if (heading.Dot(steering_direction) >= (1.0f - kRequiredForwardAngle)) {
        input.SetAction(InputAction::Backward, true);
      }

      return;
    }
  }

  bool clockwise = !leftside;

  if (has_force) {
    bool move_reverse = has_rotation || -heading.Dot(steering_direction) >= (1.0f - kRequiredForwardAngle);
    bool move_forward = has_rotation || heading.Dot(steering_direction) >= (1.0f - kRequiredForwardAngle);

    if (behind && move_reverse) {
      input.SetAction(InputAction::Backward, true);
    } else if (!behind && move_forward) {
      input.SetAction(InputAction::Forward, true);
    }
  }

  if (heading.Dot(steering_direction) < 0.996f) {
    input.SetAction(InputAction::Right, clockwise);
    input.SetAction(InputAction::Left, !clockwise);
  }
}

}  // namespace zero
