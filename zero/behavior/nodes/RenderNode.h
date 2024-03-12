#pragma once

#include <zero/RenderContext.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

#include <functional>
#include <optional>
#include <string>

namespace zero {
namespace behavior {

struct RenderTextNode : public BehaviorNode {
  struct Request {
    std::string str;
    TextColor color;
    TextAlignment alignment;
    Layer layer;

    Request(const std::string& str, TextColor color, Layer layer = Layer::TopMost,
            TextAlignment alignment = TextAlignment::Left)
        : str(str), color(color), layer(layer), alignment(alignment) {}
  };
  using FormatFunc = Request(ExecuteContext&);

  RenderTextNode(const char* camera_key, const char* position_key, FormatFunc formatter)
      : camera_key(camera_key), position_key(position_key), formatter(formatter) {}

  RenderTextNode(const char* camera_key, const Vector2f& position, FormatFunc formatter)
      : camera_key(camera_key), position(position), formatter(formatter) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_camera = ctx.blackboard.Value<Camera>(camera_key);
    if (!opt_camera) return ExecuteResult::Failure;

    Vector2f position = this->position;
    if (position_key) {
      auto opt_pos = ctx.blackboard.Value<Vector2f>(position_key);
      if (!opt_pos) return ExecuteResult::Failure;

      position = *opt_pos;
    }

    Request request = formatter(ctx);
    ctx.bot->game->sprite_renderer.PushText(*opt_camera, request.str.data(), request.color, position, request.layer,
                                            request.alignment);
    ctx.bot->game->sprite_renderer.Render(*opt_camera);

    return ExecuteResult::Success;
  }

  const char* camera_key = nullptr;
  const char* position_key = nullptr;

  Vector2f position;

  std::function<FormatFunc> formatter;
};

struct RenderRectNode : public BehaviorNode {
  RenderRectNode(const char* camera_key, const char* rect_key, const Vector3f& color)
      : camera_key(camera_key), rect_key(rect_key), color(color) {}
  RenderRectNode(const char* camera_key, const Rectangle& rectangle, const Vector3f& color)
      : camera_key(camera_key), rectangle(rectangle), color(color) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_camera = ctx.blackboard.Value<Camera>(camera_key);
    if (!opt_camera) return ExecuteResult::Failure;

    Rectangle rect = this->rectangle;
    if (rect_key) {
      auto opt_rect = ctx.blackboard.Value<Rectangle>(rect_key);
      if (!opt_rect) return ExecuteResult::Failure;

      rect = *opt_rect;
    }

    ctx.bot->game->line_renderer.PushRect(rect, color);
    ctx.bot->game->line_renderer.Render(*opt_camera);

    return ExecuteResult::Success;
  }

  const char* camera_key = nullptr;
  const char* rect_key = nullptr;
  Rectangle rectangle;
  Vector3f color;
};

struct RenderLineNode : public BehaviorNode {
  RenderLineNode(const char* camera_key, const char* line_key, const Vector3f& color)
      : camera_key(camera_key), line_key(line_key), color(color) {}
  RenderLineNode(const char* camera_key, const LineSegment& line, const Vector3f& color)
      : camera_key(camera_key), line(line), color(color) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_camera = ctx.blackboard.Value<Camera>(camera_key);
    if (!opt_camera) return ExecuteResult::Failure;

    LineSegment line = this->line;
    if (line_key) {
      auto opt_line = ctx.blackboard.Value<LineSegment>(line_key);
      if (!opt_line) return ExecuteResult::Failure;

      line = *opt_line;
    }

    ctx.bot->game->line_renderer.PushLine(line, color);
    ctx.bot->game->line_renderer.Render(*opt_camera);

    return ExecuteResult::Success;
  }

  const char* camera_key = nullptr;
  const char* line_key = nullptr;
  LineSegment line;
  Vector3f color;
};

struct RenderRayNode : public BehaviorNode {
  RenderRayNode(const char* camera_key, const char* ray_key, float length, const Vector3f& color)
      : camera_key(camera_key), ray_key(ray_key), length(length), color(color) {}
  RenderRayNode(const char* camera_key, const Ray& ray, float length, const Vector3f& color)
      : camera_key(camera_key), ray(ray), length(length), color(color) {}

  RenderRayNode(const char* camera_key, const char* ray_key, const char* length_key, const Vector3f& color)
      : camera_key(camera_key), ray_key(ray_key), length_key(length_key), color(color) {}
  RenderRayNode(const char* camera_key, const Ray& ray, const char* length_key, const Vector3f& color)
      : camera_key(camera_key), ray(ray), length_key(length_key), color(color) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_camera = ctx.blackboard.Value<Camera>(camera_key);
    if (!opt_camera) return ExecuteResult::Failure;

    Ray ray = this->ray;
    if (ray_key) {
      auto opt_ray = ctx.blackboard.Value<Ray>(ray_key);
      if (!opt_ray) return ExecuteResult::Failure;

      ray = *opt_ray;
    }

    float length = this->length;
    if (length_key) {
      auto opt_length = ctx.blackboard.Value<float>(length_key);
      if (!opt_length) return ExecuteResult::Failure;

      length = *opt_length;
    }

    LineSegment line;

    line.points[0] = ray.origin;
    line.points[1] = ray.origin + ray.direction * length;

    ctx.bot->game->line_renderer.PushLine(line, color);
    ctx.bot->game->line_renderer.Render(*opt_camera);

    return ExecuteResult::Success;
  }

  const char* camera_key = nullptr;
  const char* ray_key = nullptr;
  const char* length_key = nullptr;

  Ray ray;
  float length = 1.0f;
  Vector3f color;
};

// Renders a vector. If no origin is specified then it comes from the player's position.
struct RenderVectorNode : public BehaviorNode {
  RenderVectorNode(const char* camera_key, const Vector2f& vector, const Vector3f& color)
      : camera_key(camera_key), vector(vector), color(color), player_center(true) {}

  RenderVectorNode(const char* camera_key, const Vector2f& vector, const Vector2f& origin, const Vector3f& color)
      : camera_key(camera_key), vector(vector), origin(origin), color(color) {}

  RenderVectorNode(const char* camera_key, const Vector2f& vector, const char* origin_key, const Vector3f& color)
      : camera_key(camera_key), vector(vector), origin_key(origin_key), color(color) {}

  RenderVectorNode(const char* camera_key, const char* vector_key, const Vector3f& color)
      : camera_key(camera_key), vector_key(vector_key), color(color), player_center(true) {}

  RenderVectorNode(const char* camera_key, const char* vector_key, const Vector2f& origin, const Vector3f& color)
      : camera_key(camera_key), vector_key(vector_key), origin(origin), color(color) {}

  RenderVectorNode(const char* camera_key, const char* vector_key, const char* origin_key, const Vector3f& color)
      : camera_key(camera_key), vector_key(vector_key), origin_key(origin_key), color(color) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_camera = ctx.blackboard.Value<Camera>(camera_key);
    if (!opt_camera) return ExecuteResult::Failure;

    Vector2f vector = this->vector;
    if (vector_key) {
      auto opt_vector = ctx.blackboard.Value<Vector2f>(vector_key);
      if (!opt_vector) return ExecuteResult::Failure;

      vector = *opt_vector;
    }

    Vector2f origin = this->origin;
    if (origin_key) {
      auto opt_origin = ctx.blackboard.Value<Vector2f>(origin_key);
      if (!opt_origin) return ExecuteResult::Failure;

      vector = *opt_origin;
    } else if (player_center) {
      auto self = ctx.bot->game->player_manager.GetSelf();
      if (!self) return ExecuteResult::Failure;

      origin = self->position;
    }

    LineSegment line;

    line.points[0] = origin;
    line.points[1] = origin + vector;

    ctx.bot->game->line_renderer.PushLine(line, color);
    ctx.bot->game->line_renderer.Render(*opt_camera);

    return ExecuteResult::Success;
  }

  const char* camera_key = nullptr;
  const char* vector_key = nullptr;
  const char* origin_key = nullptr;

  Vector2f vector;
  Vector2f origin;
  Vector3f color;

  bool player_center = false;
};

}  // namespace behavior
}  // namespace zero
