#pragma once

#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

struct RayNode : public BehaviorNode {
  RayNode(const char* origin_key, const char* direction_key, const char* output_key)
      : origin_key(origin_key), direction_key(direction_key), output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_origin = ctx.blackboard.Value<Vector2f>(origin_key);
    if (!opt_origin.has_value()) return ExecuteResult::Failure;

    auto opt_direction = ctx.blackboard.Value<Vector2f>(direction_key);
    if (!opt_direction.has_value()) return ExecuteResult::Failure;

    Vector2f& origin = opt_origin.value();
    Vector2f& direction = opt_direction.value();
    Ray ray;

    ray.origin = origin;
    ray.direction = Normalize(direction);

    ctx.blackboard.Set(output_key, ray);

    return ExecuteResult::Success;
  }

  const char* origin_key;
  const char* direction_key;
  const char* output_key;
};

struct RayRectInterceptNode : public BehaviorNode {
  RayRectInterceptNode(const char* ray_key, const char* rect_key) : ray_key(ray_key), rect_key(rect_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_ray = ctx.blackboard.Value<Ray>(ray_key);
    if (!opt_ray.has_value()) return ExecuteResult::Failure;

    auto opt_rect = ctx.blackboard.Value<Rectangle>(rect_key);
    if (!opt_rect.has_value()) return ExecuteResult::Failure;

    Ray& ray = opt_ray.value();
    Rectangle& rect = opt_rect.value();

    bool intersects = RayBoxIntersect(ray, rect, nullptr, nullptr);

    return intersects ? ExecuteResult::Success : ExecuteResult::Failure;
  }

  const char* ray_key;
  const char* rect_key;
};

// Computes vector_a_key - vector_b_key and stores in output_key.
struct VectorSubtractNode : public BehaviorNode {
  VectorSubtractNode(const char* vector_a_key, const char* vector_b_key, const char* output_key, bool normalize = false)
      : vector_a_key(vector_a_key), vector_b_key(vector_b_key), output_key(output_key), normalize(normalize) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_vector_a = ctx.blackboard.Value<Vector2f>(vector_a_key);
    if (!opt_vector_a.has_value()) return ExecuteResult::Failure;

    auto opt_vector_b = ctx.blackboard.Value<Vector2f>(vector_b_key);
    if (!opt_vector_b.has_value()) return ExecuteResult::Failure;

    Vector2f vector_a = opt_vector_a.value();
    Vector2f vector_b = opt_vector_b.value();

    Vector2f result = vector_a - vector_b;

    if (normalize) {
      result = Normalize(result);
    }

    ctx.blackboard.Set(output_key, result);

    return ExecuteResult::Success;
  }

  const char* vector_a_key;
  const char* vector_b_key;
  const char* output_key;
  bool normalize;
};

struct NormalizeNode : public BehaviorNode {
  NormalizeNode(const char* input_vector_key, const char* output_vector_key)
      : input_vector_key(input_vector_key), output_vector_key(output_vector_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_input_vector = ctx.blackboard.Value<Vector2f>(input_vector_key);
    if (!opt_input_vector.has_value()) return ExecuteResult::Failure;

    Vector2f vector = opt_input_vector.value();

    ctx.blackboard.Set(output_vector_key, Normalize(vector));

    return ExecuteResult::Success;
  }

  const char* input_vector_key;
  const char* output_vector_key;
};

}  // namespace behavior
}  // namespace zero
