#pragma once

#include <zero/Math.h>
#include <zero/game/render/Shader.h>

#include <vector>

namespace zero {

struct Camera;

struct LineSegment {
  Vector2f points[2];

  LineSegment() {}
  LineSegment(const Vector2f& start, const Vector2f& end) : points{start, end} {}

  inline bool Intersects(const LineSegment& other) const {
    const Vector2f& p1 = points[0];
    const Vector2f& q1 = points[1];
    const Vector2f& p2 = other.points[0];
    const Vector2f& q2 = other.points[1];

    int o1 = GetOrientation(p1, q1, p2);
    int o2 = GetOrientation(p1, q1, q2);
    int o3 = GetOrientation(p2, q2, p1);
    int o4 = GetOrientation(p2, q2, q1);

    // General case
    if (o1 != o2 && o3 != o4) return true;

    // Special Cases
    // p1, q1 and p2 are collinear and p2 lies on segment p1q1
    if (o1 == 0 && IsOnSegment(p1, p2, q1)) return true;

    // p1, q1 and q2 are collinear and q2 lies on segment p1q1
    if (o2 == 0 && IsOnSegment(p1, q2, q1)) return true;

    // p2, q2 and p1 are collinear and p1 lies on segment p2q2
    if (o3 == 0 && IsOnSegment(p2, p1, q2)) return true;

    // p2, q2 and q1 are collinear and q1 lies on segment p2q2
    if (o4 == 0 && IsOnSegment(p2, q1, q2)) return true;

    return false;  // Doesn't fall in any of the above cases
  }

 private:
  inline static bool IsOnSegment(const Vector2f& p, const Vector2f& q, const Vector2f& r) {
    return q.x < max(p.x, r.x) && q.x > min(p.x, r.x) && q.y < max(p.y, r.y) && q.y > min(p.y, r.y);
  }

  inline static int GetOrientation(const Vector2f& p, const Vector2f& q, const Vector2f& r) {
    float val = (q.y - p.y) * (r.x - q.x) - (q.x - p.x) * (r.y - q.y);

    if (val == 0.0f) return 0;  // collinear

    return (val > 0) ? 1 : 2;  // clock or counterclock wise
  }
};

struct LineVertex {
  Vector2f position;
  Vector3f color;

  LineVertex() {}
  LineVertex(const Vector2f& pos, const Vector3f& color) : position(pos), color(color) {}
};
struct RenderableLine {
  LineVertex vertices[2];

  RenderableLine(const LineVertex& start, const LineVertex& end) : vertices{start, end} {}
};

// This renderer batches line drawing by pushing into a vector then ending with a call to Render.
// Example:
// line_renderer.PushLine(Vector2f(0, 0), Vector2f(1.0f, 0.0f, 0.0f), Vector2f(512, 512), Vector3f(1.0f, 0.0f, 0.0f));
// line_renderer.Render(ui_camera); // This pushes the draws using the ui_camera space.
struct LineRenderer {
  constexpr static size_t kVerticesPerDraw = 65535 * 8;

  GLuint vao = -1;
  GLuint vbo = -1;
  GLint mvp_uniform;
  ShaderProgram shader;
  std::vector<RenderableLine> lines;

  LineRenderer() {}
  ~LineRenderer();

  bool Initialize();

  void PushLine(const Vector2f& start, const Vector3f& start_color, const Vector2f& end, const Vector3f& end_color);
  void PushCross(const Vector2f& start, const Vector3f& color, float size = 1.0f);
  void PushRect(const Vector2f& start, const Vector2f& end, const Vector3f& color);
  void Render(Camera& camera);
};

}  // namespace zero
