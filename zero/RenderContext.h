#pragma once

#include <zero/game/Camera.h>

namespace zero {

struct SpriteRenderer;

struct RenderContext {
  Camera* game_camera = nullptr;
  Camera* ui_camera = nullptr;
  SpriteRenderer* renderer;

  RenderContext(Camera* game_camera, Camera* ui_camera, SpriteRenderer* renderer)
      : game_camera(game_camera), ui_camera(ui_camera), renderer(renderer) {}
};

}  // namespace zero
