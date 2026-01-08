#pragma once

namespace zero {

struct Game;
struct InputState;
struct Vector2f;

// Converts a steering force into actual key presses
struct Actuator {
  // Rotation threshold sets how much away from the target rotation we can be before it forces us to look within the
  // threshold. 0 would be at least the orthogonal bisected space, 1 would require perfectly aimed at the desired
  // rotation.
  void Update(Game& game, InputState& input, const Vector2f& force, float rotation, float rotation_threshold = 0.75f);

  bool enabled = true;

  // Controls whether or not we calculate the best direction forward.
  // If this is enabled, rotation + travel time along force vector is calculated for forward and reverse and the lowest
  // value is chosen.
  bool allow_reversing = true;

  // This is how close to the steering direction we must be facing to actually apply thrust.
  // 1.0 will require a perfect rotation before applying any thrust.
  // 0.0 will apply thrust if we are facing the forward plane at all.
  // -1.0 will always apply thrust.
  float required_forward_vector_to_thrust = 0.65f;
};

}  // namespace zero
