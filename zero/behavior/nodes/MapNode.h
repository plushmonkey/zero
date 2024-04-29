#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

struct VisibilityQueryNode : public BehaviorNode {
  VisibilityQueryNode(const char* position_key) : position_a_key(position_key), position_b_key(nullptr) {}
  VisibilityQueryNode(const char* position_a_key, const char* position_b_key)
      : position_a_key(position_a_key), position_b_key(position_b_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();

    if (!self) return ExecuteResult::Failure;

    auto opt_position_a = ctx.blackboard.Value<Vector2f>(position_a_key);
    if (!opt_position_a.has_value()) return ExecuteResult::Failure;

    Vector2f& position_a = opt_position_a.value();

    if (position_b_key != nullptr) {
      auto opt_position_b = ctx.blackboard.Value<Vector2f>(position_b_key);
      if (!opt_position_b.has_value()) return ExecuteResult::Failure;

      Vector2f& position_b = opt_position_b.value();

      bool hit = ctx.bot->game->GetMap().CastTo(position_a, position_b, 0xFFFF).hit;

      return hit ? ExecuteResult::Failure : ExecuteResult::Success;
    }

    bool hit = ctx.bot->game->GetMap().CastTo(self->position, position_a, self->frequency).hit;

    return hit ? ExecuteResult::Failure : ExecuteResult::Success;
  }

  const char* position_a_key;
  const char* position_b_key;
};

struct TileQueryNode : public BehaviorNode {
  TileQueryNode(TileId id) : id(id) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return ExecuteResult::Failure;

    TileId occupied_id = ctx.bot->game->GetMap().GetTileId(self->position);

    return occupied_id == id ? ExecuteResult::Success : ExecuteResult::Failure;
  }

  TileId id;
};

// Returns success if the distance between the positions is >= the provided threshold.
struct DistanceThresholdNode : public BehaviorNode {
  DistanceThresholdNode(const char* position_key, float threshold)
      : position_a_key(position_key), position_b_key(nullptr), threshold_sq(threshold * threshold) {}
  DistanceThresholdNode(const char* position_a_key, const char* position_b_key, float threshold)
      : position_a_key(position_a_key), position_b_key(position_b_key), threshold_sq(threshold * threshold) {}
  DistanceThresholdNode(const char* position_a_key, const char* position_b_key, const char* threshold_key)
      : position_a_key(position_a_key), position_b_key(position_b_key), threshold_key(threshold_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_position_a = ctx.blackboard.Value<Vector2f>(position_a_key);
    if (!opt_position_a.has_value()) return ExecuteResult::Failure;

    Vector2f& position_a = opt_position_a.value();

    Vector2f position_b;

    if (position_b_key) {
      auto opt_position_b = ctx.blackboard.Value<Vector2f>(position_b_key);
      if (!opt_position_b.has_value()) return ExecuteResult::Failure;

      position_b = opt_position_b.value();
    } else {
      auto self = ctx.bot->game->player_manager.GetSelf();
      if (!self) return ExecuteResult::Failure;

      position_b = self->position;
    }

    float threshold = threshold_sq;
    if (threshold_key) {
      auto opt_threshold = ctx.blackboard.Value<float>(threshold_key);
      if (!opt_threshold) return ExecuteResult::Failure;
      threshold = *opt_threshold * *opt_threshold;
    }

    return position_a.DistanceSq(position_b) >= threshold ? ExecuteResult::Success : ExecuteResult::Failure;
  }

  const char* position_a_key = nullptr;
  const char* position_b_key = nullptr;
  const char* threshold_key = nullptr;
  float threshold_sq = 0.0f;
};

// Finds the closest tile and stores it in the provided key.
struct ClosestTileQueryNode : public BehaviorNode {
  ClosestTileQueryNode(const char* tile_vector_key, const char* closest_key)
      : position_key(nullptr), tile_vector_key(tile_vector_key), closest_key(closest_key) {}
  ClosestTileQueryNode(const char* position_key, const char* tile_vector_key, const char* closest_key)
      : position_key(position_key), tile_vector_key(tile_vector_key), closest_key(closest_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    Vector2f from_position;

    if (position_key) {
      auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
      if (!opt_position.has_value()) return ExecuteResult::Failure;

      from_position = opt_position.value();
    } else {
      auto self = ctx.bot->game->player_manager.GetSelf();
      if (!self) return ExecuteResult::Failure;

      from_position = self->position;
    }

    auto opt_tile_vector = ctx.blackboard.Value<std::vector<Vector2f>>(tile_vector_key);
    if (!opt_tile_vector.has_value()) return ExecuteResult::Failure;

    std::vector<Vector2f>& tile_vector = opt_tile_vector.value();

    if (tile_vector.empty()) return ExecuteResult::Failure;

    Vector2f closest = tile_vector[0];
    float closest_distance_sq = tile_vector[0].DistanceSq(from_position);

    for (Vector2f& tile : tile_vector) {
      float dist_sq = tile.DistanceSq(from_position);
      if (dist_sq < closest_distance_sq) {
        closest_distance_sq = dist_sq;
        closest = tile;
      }
    }

    ctx.blackboard.Set(closest_key, closest);

    return ExecuteResult::Success;
  }

  const char* position_key;
  const char* tile_vector_key;
  const char* closest_key;
};

}  // namespace behavior
}  // namespace zero
