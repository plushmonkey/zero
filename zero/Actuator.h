#pragma once

#include <zero/game/Game.h>

namespace zero {

// Converts a steering force into actual key presses
struct Actuator {
  void Update(Game& game, InputState& input, const Vector2f& force, float rotation);
};

}  // namespace zero
