#ifndef ZERO_RENDER_BACKGROUNDRENDERER_H_
#define ZERO_RENDER_BACKGROUNDRENDERER_H_

#include <zero/Math.h>
#include <zero/Types.h>
#include <zero/game/render/Shader.h>
#include <zero/game/render/Sprite.h>

namespace zero {

struct Camera;
struct MemoryArena;
struct SpriteRenderer;

struct BackgroundRenderer {
  // Set of textures with stars in them that get transformed for the background
  GLuint textures[8];

  SpriteRenderable* renderables = nullptr;

  bool Initialize(MemoryArena& perm_arena, MemoryArena& temp_arena, const Vector2f& surface_dim);
  void Cleanup();

  void Render(Camera& camera, SpriteRenderer& renderer, const Vector2f& surface_dim);

 private:
  void RenderParallaxLayer(Camera& camera, SpriteRenderer& renderer, const Vector2f& surface_dim, float speed,
                           int layer);
  void GenerateTextures(MemoryArena& temp_arena, GLuint* textures, size_t texture_count, u8 color);
};

}  // namespace zero

#endif
