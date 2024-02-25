#ifndef ZERO_CAMERA_H_
#define ZERO_CAMERA_H_

#include <zero/Math.h>
#include <zero/game/render/Layer.h>

namespace zero {

struct Camera {
  Vector2f position;
  mat4 projection;
  Vector2f surface_dim;
  float scale;

  Camera(const Vector2f& surface_dim, const Vector2f& position, float scale)
      : surface_dim(surface_dim), position(position), scale(scale) {
    float zmax = (float)Layer::Count;
    projection = Orthographic(-surface_dim.x / 2.0f * scale, surface_dim.x / 2.0f * scale, surface_dim.y / 2.0f * scale,
                              -surface_dim.y / 2.0f * scale, -zmax, zmax);
  }

  mat4 GetView() { return Translate(mat4::Identity(), Vector3f(-position.x, -position.y, 0.0f)); }
  mat4 GetProjection() { return projection; }
};

}  // namespace zero

#endif
