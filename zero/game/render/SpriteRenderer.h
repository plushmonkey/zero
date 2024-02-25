#ifndef ZERO_RENDER_SPRITERENDERER_H_
#define ZERO_RENDER_SPRITERENDERER_H_

#include <zero/Math.h>
#include <zero/game/Memory.h>
#include <zero/game/render/Layer.h>
#include <zero/game/render/Shader.h>
#include <zero/game/render/Sprite.h>
#include <zero/game/render/TextureMap.h>

namespace zero {

struct Camera;

enum class TextColor { White, Green, Blue, DarkRed, Yellow, Fuschia, Red, Pink };
enum class TextAlignment { Left, Center, Right };

// TODO: Should there be async lazy texture loading? - No for now. Textures will need to be reloaded later for lvz
// TODO: Should a texture atlas be generated? - Probably not. I don't think binding performance will be a concern for a
// game this simple.

// This uses a push buffer where the texture of the sprites being pushed don't need to be contiguous, but
// that does increase performance. It binds the texture as long as possible, so batching a bunch of sprites from the
// same sheet together is ideal.
//
// The push buffer data is only the actual render data, so it's appropriate for just one camera.
// You should flush the buffer with a SpriteRenderer::Render call with the appropriate camera after filling it.
//
// For example:
// renderer.PushText(ui_camera, "text", ...);
// renderer.PushText(ui_camera, "text2", ...);
// renderer.Render(ui_camera); // This call will flush both the text calls to the graphics card as actual draw commands.
struct SpriteRenderer {
  MemoryArena push_buffer;
  // Store the texture and the vertex data in separate push buffers so batch pushing is simplified.
  MemoryArena texture_push_buffer;

  ShaderProgram shader;

  GLint color_uniform = -1;
  GLint mvp_uniform = -1;

  GLuint vao = -1;
  GLuint vbo = -1;

  size_t renderable_count = 0;
  SpriteRenderable renderables[65535];

  size_t texture_count = 0;
  GLuint textures[1024];

  TextureMap* texture_map = nullptr;

  bool Initialize(MemoryArena& perm_arena);
  SpriteRenderable* CreateSheet(TextureData* texture_data, const Vector2f& dimensions, int* count);
  SpriteRenderable* LoadSheet(const char* filename, const Vector2f& dimensions, int* count);
  SpriteRenderable* LoadSheetFromMemory(const char* name, const u8* data, int width, int height,
                                        const Vector2f& dimensions, int* count);
  void FreeSheet(unsigned int texture_id);

  GLuint CreateTexture(const char* name, const u8* data, int width, int height);

  // Position can be either in world space or screen space depending on renderer setup
  void Draw(Camera& camera, const SpriteRenderable& renderable, const Vector2f& position, Layer layer);
  void Draw(Camera& camera, const SpriteRenderable& renderable, const Vector3f& position);
  void PushText(Camera& camera, const char* text, TextColor color, const Vector2f& position, Layer layer,
                TextAlignment alignment = TextAlignment::Left);

  void Render(Camera& camera);

  void Cleanup();
};

}  // namespace zero

#endif
