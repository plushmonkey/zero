#pragma once

#include <zero/Math.h>

#include <vector>

namespace zero {
namespace path {

struct Path {
  size_t index = 0;
  std::vector<Vector2f> points;
  bool dynamic = false;

  inline void Clear() {
    points.clear();
    index = 0;
    dynamic = false;
  }

  inline Vector2f Advance() {
    if (Empty()) return Vector2f();

    if (index <= points.size() - 1) {
      ++index;
    }

    if (index >= points.size() - 1) return GetGoal();

    return points[index];
  }

  inline bool IsDone() const { return points.empty() || index > points.size() - 1; }
  inline bool IsOnGoalNode() const { return !points.empty() && index == points.size() - 1; }

  inline Vector2f GetCurrent() const {
    if (points.empty()) return Vector2f();
    if (index >= points.size() - 1) return GetGoal();

    return points[index];
  }

  inline Vector2f GetNext() const {
    if (points.empty()) return Vector2f();
    if (index >= points.size() - 1) return GetGoal();

    return points[index + 1];
  }

  inline Vector2f GetStart() const {
    if (points.empty()) return Vector2f();

    return points.front();
  }

  inline Vector2f GetGoal() const {
    if (points.empty()) return Vector2f();

    return points.back();
  }

  inline bool Empty() const { return points.empty(); }
  inline void Add(Vector2f point) { points.push_back(point); }

  inline bool IsCurrentTile(Vector2f tile) {
    if (Empty()) return false;
    Vector2f current = GetCurrent();

    return (u16)current.x == (u16)tile.x && (u16)current.y == (u16)tile.y;
  }

  inline float GetRemainingDistance() {
    if (points.empty()) return 0.0f;
    if (index >= points.size() - 1) return 0.0f;

    float dist = 0.0f;

    for (size_t i = index; i < points.size() - 1; ++i) {
      dist += points[i].Distance(points[i + 1]);
    }

    return dist;
  }

  inline bool Contains(s32 check_x, s32 check_y) const {
    if (points.empty()) return false;
    if (index >= points.size() - 1) return false;

    for (size_t i = index; i < points.size() - 1; ++i) {
      s32 x = (s32)points[i].x;
      s32 y = (s32)points[i].y;

      if (x == check_x && y == check_y) {
        return true;
      }
    }

    return false;
  }
};

}  // namespace path
}  // namespace zero
