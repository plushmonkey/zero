#pragma once

#include <zero/Math.h>

#include <vector>

namespace zero {
namespace path {

struct Path {
  size_t index = 0;
  std::vector<Vector2f> points;

  inline void Clear() {
    points.clear();
    index = 0;
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
};

}  // namespace path
}  // namespace zero
