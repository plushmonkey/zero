#include "LineRenderer.h"

#include <zero/game/Camera.h>

namespace zero {

const char* kLineVertexShaderCode = R"(
#version 330

layout (location = 0) in vec2 position;
layout (location = 1) in vec3 color;

uniform mat4 mvp;

out vec2 varying_position;
out vec3 varying_color;

void main() {
  gl_Position = mvp * vec4(position, 14.0, 1.0);
  varying_position = position;
  varying_color = color;
}
)";

const char* kLineFragmentShaderCode = R"(
#version 330

in vec2 varying_position;
in vec3 varying_color;

out vec4 color;

void main() {
  color = vec4(varying_color, 1.0);
}
)";

LineRenderer::~LineRenderer() {
  glDeleteBuffers(1, &vbo);
  glDeleteVertexArrays(1, &vao);
}

bool LineRenderer::Initialize() {
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);

  GLsizeiptr vbo_size = sizeof(LineVertex) * kVerticesPerDraw;
  glBufferData(GL_ARRAY_BUFFER, vbo_size, nullptr, GL_DYNAMIC_DRAW);

  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(LineVertex), 0);
  glEnableVertexAttribArray(0);

  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), (void*)offsetof(LineVertex, color));
  glEnableVertexAttribArray(1);

  if (!shader.Initialize(kLineVertexShaderCode, kLineFragmentShaderCode)) {
    fprintf(stderr, "Failed to create line shader.\n");
  }

  mvp_uniform = glGetUniformLocation(shader.program, "mvp");

  return true;
}

void LineRenderer::PushLine(const Vector2f& start, const Vector3f& start_color, const Vector2f& end,
  const Vector3f& end_color) {
  lines.emplace_back(LineVertex(start, start_color), LineVertex(end, end_color));
}

void LineRenderer::PushCross(const Vector2f& start, const Vector3f& color, float size) {
  float radius = size * 0.5f;

  Vector2f top_left = start;
  Vector2f top_right(start.x + size, start.y);
  Vector2f bottom_left(start.x, start.y + size);
  Vector2f bottom_right(start.x + size, start.y + size);

  Vector2f r(radius, radius);

  PushLine(top_left - r, color, bottom_right - r, color);
  PushLine(bottom_left - r, color, top_right - r, color);
}

void LineRenderer::PushRect(const Vector2f& start, const Vector2f& end, const Vector3f& color) {
  Vector2f top_left(start.x, start.y);
  Vector2f top_right(end.x, start.y);
  Vector2f bottom_left(start.x, end.y);
  Vector2f bottom_right(end.x, end.y);

  PushLine(top_left, color, top_right, color);
  PushLine(top_left, color, bottom_left, color);

  PushLine(bottom_right, color, top_right, color);
  PushLine(bottom_right, color, bottom_left, color);
}

void LineRenderer::Render(Camera& camera) {
  if (lines.empty()) return;
  if (shader.program == -1) {
    lines.clear();
    return;
  }

  glUseProgram(shader.program);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);

  mat4 proj = camera.GetProjection();
  mat4 view = camera.GetView();
  mat4 mvp = proj * view;

  glUniformMatrix4fv(mvp_uniform, 1, GL_FALSE, (const GLfloat*)mvp.data);

  size_t total_lines = lines.size();
  size_t line_offset = 0;

  while (line_offset < total_lines) {
    GLuint line_count = kVerticesPerDraw / 2;
    size_t remaining_lines = total_lines - line_offset;

    if (remaining_lines < line_count) {
      line_count = (GLuint)remaining_lines;
    }

    GLuint vertex_count = line_count * 2;

    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_count * sizeof(LineVertex), lines.data() + line_offset);
    glDrawArrays(GL_LINES, 0, vertex_count);

    line_offset += line_count;
  }

  lines.clear();
}

}  // namespace elm
