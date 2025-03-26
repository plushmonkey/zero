#pragma once

#include <zero/game/Game.h>

namespace zero {

// Converts a steering force into actual key presses
struct Actuator {
  // Rotation threshold sets how much away from the target rotation we can be before it forces us to look within the threshold.
  // 0 would be at least the orthogonal bisected space, 1 would require perfectly aimed at the desired rotation.
  void Update(Game& game, InputState& input, const Vector2f& force, float rotation, float rotation_threshold = 0.75f);

  bool enabled = true;
};

}  // namespace zero
