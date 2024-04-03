#pragma once

#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

#include <random>

namespace zero {
namespace behavior {

template <typename T>
struct EqualityNode : public BehaviorNode {
  EqualityNode(const T& a_value, const char* b_key) : a_value(a_value), b_key(b_key) {}
  EqualityNode(const char* b_key, const T& a_value) : a_value(a_value), b_key(b_key) {}
  EqualityNode(const char* a_key, const char* b_key) : a_key(a_key), b_key(b_key), a_value(T()) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    if (!b_key) return ExecuteResult::Failure;

    auto opt_b = ctx.blackboard.Value<T>(b_key);
    if (!opt_b) return ExecuteResult::Failure;

    T a_check = a_value;

    if (a_key) {
      auto opt_a = ctx.blackboard.Value<T>(a_key);
      if (!opt_a) return ExecuteResult::Failure;
      a_check = *opt_a;
    }

    return a_check == *opt_b ? ExecuteResult::Success : ExecuteResult::Failure;
  }

  T a_value;
  const char* a_key = nullptr;
  const char* b_key = nullptr;
};

template <typename T>
struct RandomIntNode : public BehaviorNode {
  RandomIntNode(T min, T max, const char* output_key) : min(min), max(max), output_key(output_key) {}
  RandomIntNode(T min, const char* max_key, const char* output_key)
      : min(min), max_key(max_key), output_key(output_key) {}
  RandomIntNode(const char* min_key, T max, const char* output_key)
      : min_key(min_key), max(max), output_key(output_key) {}
  RandomIntNode(const char* min_key, const char* max_key, const char* output_key)
      : min_key(min_key), max_key(max_key), output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    T min = this->min;
    T max = this->max;

    if (min_key) {
      auto opt_min = ctx.blackboard.Value<T>(min_key);
      if (!opt_min.has_value()) return ExecuteResult::Failure;

      min = opt_min.value();
    }

    if (max_key) {
      auto opt_max = ctx.blackboard.Value<T>(max_key);
      if (!opt_max.has_value()) return ExecuteResult::Failure;

      max = opt_max.value();
    }

    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<T> dist(min, max);

    T random_value = dist(rng);

    ctx.blackboard.Set<T>(output_key, random_value);

    return ExecuteResult::Success;
  }

  T min = {};
  T max = {};

  const char* min_key = nullptr;
  const char* max_key = nullptr;
  const char* output_key = nullptr;
};

struct RandomNode : public BehaviorNode {
  RandomNode(float min, float max, const char* output_key) : min(min), max(max), output_key(output_key) {}
  RandomNode(float min, const char* max_key, const char* output_key)
      : min(min), max_key(max_key), output_key(output_key) {}
  RandomNode(const char* min_key, float max, const char* output_key)
      : min_key(min_key), max(max), output_key(output_key) {}
  RandomNode(const char* min_key, const char* max_key, const char* output_key)
      : min_key(min_key), max_key(max_key), output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    float min = this->min;
    float max = this->max;

    if (min_key) {
      auto opt_min = ctx.blackboard.Value<float>(min_key);
      if (!opt_min.has_value()) return ExecuteResult::Failure;

      min = opt_min.value();
    }

    if (max_key) {
      auto opt_max = ctx.blackboard.Value<float>(max_key);
      if (!opt_max.has_value()) return ExecuteResult::Failure;

      max = opt_max.value();
    }

    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_real_distribution<float> dist(min, max);

    float random_value = dist(rng);

    ctx.blackboard.Set(output_key, random_value);

    return ExecuteResult::Success;
  }

  float min = {};
  float max = {};

  const char* min_key = nullptr;
  const char* max_key = nullptr;
  const char* output_key = nullptr;
};

struct ScalarNode : public BehaviorNode {
  ScalarNode(const char* existing_key, const char* output_key) : existing_key(existing_key), output_key(output_key) {}
  ScalarNode(float value, const char* output_key) : value(value), existing_key(nullptr), output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    float input = value;

    if (existing_key) {
      auto opt_input = ctx.blackboard.Value<float>(existing_key);
      if (!opt_input.has_value()) return ExecuteResult::Failure;

      input = opt_input.value();
    }

    ctx.blackboard.Set(output_key, input);

    return ExecuteResult::Success;
  }

  const char* existing_key;
  const char* output_key;
  float value = 0.0f;
};

struct VectorNode : public BehaviorNode {
  VectorNode(const char* existing_vector_key, const char* output_key)
      : existing_vector_key(existing_vector_key), output_key(output_key) {}
  VectorNode(const Vector2f& vector, const char* output_key)
      : vector(vector), existing_vector_key(nullptr), output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    Vector2f input = vector;

    if (existing_vector_key) {
      auto opt_input = ctx.blackboard.Value<Vector2f>(existing_vector_key);
      if (!opt_input.has_value()) return ExecuteResult::Failure;

      input = opt_input.value();
    }

    ctx.blackboard.Set(output_key, input);

    return ExecuteResult::Success;
  }

  const char* existing_vector_key;
  const char* output_key;
  Vector2f vector;
};

struct PerpendicularNode : public BehaviorNode {
  PerpendicularNode(Vector2f position1, Vector2f position2, const char* output_key, bool normalize = false)
      : position1(position1), position2(position2), output_key(output_key), normalize(normalize) {}
  PerpendicularNode(const char* position_key1, Vector2f position2, const char* output_key, bool normalize = false)
      : position_key1(position_key1), position2(position2), output_key(output_key), normalize(normalize) {}
  PerpendicularNode(Vector2f position1, const char* position_key2, const char* output_key, bool normalize = false)
      : position1(position1), position_key2(position_key2), output_key(output_key), normalize(normalize) {}
  PerpendicularNode(const char* position_key1, const char* position_key2, const char* output_key,
                    bool normalize = false)
      : position_key1(position_key1), position_key2(position_key2), output_key(output_key), normalize(normalize) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    Vector2f pos1 = position1;
    Vector2f pos2 = position2;

    if (position_key1) {
      auto opt_pos = ctx.blackboard.Value<Vector2f>(position_key1);
      if (!opt_pos.has_value()) return ExecuteResult::Failure;
      pos1 = opt_pos.value();
    }

    if (position_key2) {
      auto opt_pos = ctx.blackboard.Value<Vector2f>(position_key2);
      if (!opt_pos.has_value()) return ExecuteResult::Failure;
      pos2 = opt_pos.value();
    }

    Vector2f result = Perpendicular(pos1 - pos2);

    if (normalize) {
      result = Normalize(result);
    }

    ctx.blackboard.Set<Vector2f>(output_key, result);

    return ExecuteResult::Success;
  }

  Vector2f position1;
  Vector2f position2;

  bool normalize = false;

  const char* position_key1 = nullptr;
  const char* position_key2 = nullptr;
  const char* output_key = nullptr;
};

struct MoveRectangleNode : public BehaviorNode {
  MoveRectangleNode(const char* rectangle_key, const Vector2f& new_position, const char* output_key)
      : rectangle_key(rectangle_key), new_position(new_position), output_key(output_key) {}
  MoveRectangleNode(const char* rectangle_key, const char* position_key, const char* output_key)
      : rectangle_key(rectangle_key), position_key(position_key), output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    Vector2f new_position = this->new_position;

    if (position_key) {
      auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
      if (!opt_position.has_value()) return ExecuteResult::Failure;

      new_position = opt_position.value();
    }

    auto opt_rectangle = ctx.blackboard.Value<Rectangle>(rectangle_key);
    if (!opt_rectangle.has_value()) return ExecuteResult::Failure;

    Rectangle rectangle = opt_rectangle.value();
    Vector2f full_extents = rectangle.max - rectangle.min;
    Vector2f half_extents = full_extents * 0.5f;

    rectangle.min = new_position - half_extents;
    rectangle.max = new_position + half_extents;

    ctx.blackboard.Set(output_key, rectangle);

    return ExecuteResult::Success;
  }

  Vector2f new_position;
  const char* rectangle_key;
  const char* output_key;
  const char* position_key = nullptr;
};

struct RectangleNode : public BehaviorNode {
  RectangleNode(const char* center_position_key, const char* half_extent_vector_key, const char* output_key)
      : center_position_key(center_position_key),
        half_extent_vector_key(half_extent_vector_key),
        output_key(output_key) {}

  RectangleNode(const char* center_position_key, const Vector2f& half_extent, const char* output_key)
      : center_position_key(center_position_key),
        half_extent(half_extent),
        half_extent_vector_key(nullptr),
        output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_center = ctx.blackboard.Value<Vector2f>(center_position_key);
    if (!opt_center.has_value()) return ExecuteResult::Failure;

    Vector2f half_extent = this->half_extent;

    if (half_extent_vector_key) {
      auto opt_extent = ctx.blackboard.Value<Vector2f>(half_extent_vector_key);
      if (!opt_extent.has_value()) return ExecuteResult::Failure;

      half_extent = opt_extent.value();
    }

    Vector2f& center = opt_center.value();
    Rectangle rect;

    rect.min = center - half_extent;
    rect.max = center + half_extent;

    ctx.blackboard.Set(output_key, rect);

    return ExecuteResult::Success;
  }

  const char* center_position_key;
  const char* half_extent_vector_key;
  const char* output_key;

  Vector2f half_extent;
};

struct RectangleContainsNode : public BehaviorNode {
  RectangleContainsNode(const char* rect_key, const char* position_key)
      : rect_key(rect_key), position_key(position_key) {}
  RectangleContainsNode(const char* rect_key, Vector2f position) : rect_key(rect_key), position(position) {}
  RectangleContainsNode(Rectangle rect, const char* position_key) : rect(rect), position_key(position_key) {}
  RectangleContainsNode(Rectangle rect, Vector2f position) : rect(rect), position(position) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    Vector2f check_position = position;

    if (position_key) {
      auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
      if (!opt_position.has_value()) return ExecuteResult::Failure;

      check_position = opt_position.value();
    }

    Rectangle check_rect = rect;

    if (rect_key) {
      auto opt_rect = ctx.blackboard.Value<Rectangle>(rect_key);
      if (!opt_rect.has_value()) return ExecuteResult::Failure;

      check_rect = opt_rect.value();
    }

    return check_rect.Contains(check_position) ? ExecuteResult::Success : ExecuteResult::Failure;
  }

  const char* position_key = nullptr;
  Vector2f position;

  const char* rect_key = nullptr;
  Rectangle rect;
};

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

struct RayRectangleInterceptNode : public BehaviorNode {
  RayRectangleInterceptNode(const char* ray_key, const char* rect_key) : ray_key(ray_key), rect_key(rect_key) {}

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

template <typename T>
struct ScalarThresholdNode : public BehaviorNode {
  ScalarThresholdNode(const char* scalar_key, T threshold) : scalar_key(scalar_key), threshold(threshold) {}
  ScalarThresholdNode(const char* scalar_key, const char* threshold_key)
      : scalar_key(scalar_key), threshold_key(threshold_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_scalar = ctx.blackboard.Value<T>(scalar_key);
    if (!opt_scalar.has_value()) return ExecuteResult::Failure;

    T scalar = opt_scalar.value();
    T threshold = this->threshold;

    if (threshold_key) {
      auto opt_threshold = ctx.blackboard.Value<T>(threshold_key);
      if (!opt_threshold.has_value()) return ExecuteResult::Failure;

      threshold = opt_threshold.value();
    }

    return scalar >= threshold ? ExecuteResult::Success : ExecuteResult::Failure;
  }

  const char* scalar_key = nullptr;
  const char* threshold_key = nullptr;
  T threshold = {};
};

struct VectorDotNode : public BehaviorNode {
  VectorDotNode(const char* vector_a_key, const char* vector_b_key, const char* output_key, bool normalize = false)
      : vector_a_key(vector_a_key), vector_b_key(vector_b_key), output_key(output_key), normalize(normalize) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_vector_a = ctx.blackboard.Value<Vector2f>(vector_a_key);
    if (!opt_vector_a.has_value()) return ExecuteResult::Failure;

    auto opt_vector_b = ctx.blackboard.Value<Vector2f>(vector_b_key);
    if (!opt_vector_b.has_value()) return ExecuteResult::Failure;

    Vector2f vector_a = opt_vector_a.value();
    Vector2f vector_b = opt_vector_b.value();

    if (normalize) {
      vector_a = Normalize(vector_a);
      vector_b = Normalize(vector_b);
    }

    float result = vector_a.Dot(vector_b);

    ctx.blackboard.Set(output_key, result);

    return ExecuteResult::Success;
  }

  const char* vector_a_key;
  const char* vector_b_key;
  const char* output_key;
  bool normalize;
};

// Computes vector_a_key + vector_b_key and stores in output_key.
struct VectorAddNode : public BehaviorNode {
  VectorAddNode(const char* vector_a_key, const char* vector_b_key, const char* output_key, bool normalize = false)
      : vector_a_key(vector_a_key), vector_b_key(vector_b_key), output_key(output_key), normalize(normalize) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_vector_a = ctx.blackboard.Value<Vector2f>(vector_a_key);
    if (!opt_vector_a.has_value()) return ExecuteResult::Failure;

    auto opt_vector_b = ctx.blackboard.Value<Vector2f>(vector_b_key);
    if (!opt_vector_b.has_value()) return ExecuteResult::Failure;

    Vector2f vector_a = opt_vector_a.value();
    Vector2f vector_b = opt_vector_b.value();

    Vector2f result = vector_a + vector_b;

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

struct DistanceNode : public BehaviorNode {
  DistanceNode(const char* vector_a_key, const char* vector_b_key, const char* output_float_key, bool squared = false)
      : vector_a_key(vector_a_key), vector_b_key(vector_b_key), output_float_key(output_float_key), squared(squared) {}

  DistanceNode(Vector2f vector_a, const char* vector_b_key, const char* output_float_key, bool squared = false)
      : vector_a_static(vector_a), vector_b_key(vector_b_key), output_float_key(output_float_key), squared(squared) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    Vector2f vector_a = vector_a_static;

    if (vector_a_key) {
      auto opt_vector_a = ctx.blackboard.Value<Vector2f>(vector_a_key);
      if (!opt_vector_a.has_value()) return ExecuteResult::Failure;

      vector_a = opt_vector_a.value();
    }

    auto opt_vector_b = ctx.blackboard.Value<Vector2f>(vector_b_key);
    if (!opt_vector_b.has_value()) return ExecuteResult::Failure;

    Vector2f vector_b = opt_vector_b.value();

    float distance = squared ? vector_a.DistanceSq(vector_b) : vector_a.Distance(vector_b);
    ctx.blackboard.Set(output_float_key, distance);

    return ExecuteResult::Success;
  }

  Vector2f vector_a_static;

  const char* vector_a_key = nullptr;
  const char* vector_b_key = nullptr;

  const char* output_float_key = nullptr;
  bool squared = false;
};

}  // namespace behavior
}  // namespace zero
