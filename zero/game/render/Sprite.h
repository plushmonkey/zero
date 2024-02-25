#ifndef ZERO_RENDER_SPRITE_H_
#define ZERO_RENDER_SPRITE_H_

#include <zero/Math.h>

namespace zero {

struct SpriteRenderable {
  Vector2f uvs[4];
  Vector2f dimensions;
  unsigned int texture = 0xFFFFFFFF;
};

}  // namespace zero

#endif
