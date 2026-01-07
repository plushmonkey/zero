#pragma once

#include <zero/Math.h>
#include <zero/game/Game.h>

namespace zero {

struct Steering {
  Vector2f force;
  float rotation = 0.0f;
  float rotation_threshold = 0.75f;

  void Reset() {
    force = Vector2f(0, 0);
    rotation = 0.0f;
    rotation_threshold = 0.75f;
  }

  void Face(Game& game, const Vector2f& target);

  inline void SetRotationThreshold(float threshold) { this->rotation_threshold = threshold; }

  void SeekZero(Game& game);

  void Seek(Game& game, const Vector2f& target);
  void Seek(Game& game, const Vector2f& target, float target_distance);
  void Arrive(Game& game, const Vector2f& target, float slow_radius, float min_multiplier = 0.0f);
  void Pursue(Game& game, const Vector2f& target_position, const Player& target, float target_distance);

  void AvoidWalls(Game& game);

  // Repel self from teammate to avoid synchronized movements
  void AvoidTeam(Game& game, float dist);

  // Repel self from enemy
  void AvoidEnemy(Game& game, float dist);
};

}  // namespace zero
