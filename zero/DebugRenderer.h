#pragma once

#include <zero/Types.h>
#include <zero/game/Game.h>

struct GLFWwindow;

namespace zero {

struct DebugRenderer {
  GLFWwindow* window = nullptr;

  bool Initialize(s32 surface_width, s32 surface_height);
  void Close();

  // Returns false if the window is closed and should be terminated.
  bool Render(Game& game, float dt);
};

}  // namespace zero
