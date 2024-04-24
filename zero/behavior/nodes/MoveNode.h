#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>
#include <zero/game/Logger.h>

namespace zero {
namespace behavior {

struct SeekNode : public BehaviorNode {
  enum class DistanceResolveType { Static, Zero, Dynamic };
  SeekNode(const char* position_key) : position_key(position_key) {}
  SeekNode(const char* position_key, float target_distance, DistanceResolveType type)
      : position_key(position_key), target_distance(target_distance), distance_type(type) {}
  SeekNode(const char* position_key, const char* target_distance_key)
      : position_key(position_key), target_distance_key(target_distance_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
    if (!opt_position.has_value()) return ExecuteResult::Failure;

    Vector2f position = opt_position.value();

    float target_distance = this->target_distance;

    if (target_distance_key) {
      auto opt_distance = ctx.blackboard.Value<float>(target_distance_key);
      if (!opt_distance) return ExecuteResult::Failure;

      target_distance = *opt_distance;
    }

    if (target_distance > 0.0f) {
      auto self = ctx.bot->game->player_manager.GetSelf();
      Vector2f to_target = position - self->position;

      if (to_target.LengthSq() <= target_distance * target_distance) {
        if (distance_type == DistanceResolveType::Zero) {
          ctx.bot->bot_controller->steering.SeekZero(*ctx.bot->game);
        } else if (distance_type == DistanceResolveType::Dynamic) {
          ctx.bot->bot_controller->steering.Seek(*ctx.bot->game, position, target_distance);
        }
      } else {
        ctx.bot->bot_controller->steering.Seek(*ctx.bot->game, position, target_distance);
      }
    } else {
      ctx.bot->bot_controller->steering.Seek(*ctx.bot->game, position);
    }

    return ExecuteResult::Success;
  }

  DistanceResolveType distance_type = DistanceResolveType::Zero;
  float target_distance = 0.0f;
  const char* position_key = nullptr;
  const char* target_distance_key = nullptr;
};

struct SeekZeroNode : public BehaviorNode {
  ExecuteResult Execute(ExecuteContext& ctx) override {
    ctx.bot->bot_controller->steering.SeekZero(*ctx.bot->game);
    return ExecuteResult::Success;
  }
};

struct ArriveNode : public BehaviorNode {
  ArriveNode(const char* position_key, float deceleration) : position_key(position_key), deceleration(deceleration) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
    if (!opt_position.has_value()) return ExecuteResult::Failure;

    Vector2f position = opt_position.value();

    ctx.bot->bot_controller->steering.Arrive(*ctx.bot->game, position, deceleration);

    return ExecuteResult::Success;
  }

  const char* position_key;
  float deceleration;
};

struct FaceNode : public BehaviorNode {
  FaceNode(const char* position_key) : position_key(position_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
    if (!opt_position.has_value()) return ExecuteResult::Failure;

    Vector2f position = opt_position.value();

    ctx.bot->bot_controller->steering.Face(*ctx.bot->game, position);

    return ExecuteResult::Success;
  }

  const char* position_key;
};

struct GoToNode : public BehaviorNode {
  GoToNode(const char* position_key) : position_key(position_key) {}
  GoToNode(Vector2f position) : position(position), position_key(nullptr) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return ExecuteResult::Failure;

    Vector2f target = position;

    if (position_key) {
      auto opt_pos = ctx.blackboard.Value<Vector2f>(position_key);
      if (!opt_pos.has_value()) return ExecuteResult::Failure;

      target = opt_pos.value();
    }

    auto& map = ctx.bot->game->GetMap();
    auto& current_path = ctx.bot->bot_controller->current_path;
    auto& pathfinder = ctx.bot->bot_controller->pathfinder;
    auto& game = *ctx.bot->game;
    bool build = true;

    float radius = game.connection.settings.ShipSettings[self->ship].GetRadius();

    if (!current_path.Empty()) {
      if (target.DistanceSq(current_path.GetGoal()) <= 3.0f * 3.0f) {
        Vector2f next = current_path.GetCurrent();

        CastResult cast = map.CastShip(self, radius, next);

        // Try to walk the path backwards to re-use the nodes.
        while (cast.hit && current_path.index > 0) {
          --current_path.index;
          next = current_path.GetCurrent();
          cast = map.CastShip(self, radius, next);
        }

        if (!cast.hit) {
          build = false;
        }
      }
    }

    if (build) {
      current_path = pathfinder->FindPath(game.connection.map, self->position, target, radius);
      if (current_path.points.size() > 10) {
        Log(LogLevel::Jabber, "Rebuilding path");
      }
    }

    if (current_path.IsDone()) {
      return ExecuteResult::Success;
    }

    if (current_path.Empty()) {
      return ExecuteResult::Failure;
    }

    Vector2f movement_target = current_path.GetCurrent();

    // Cull future nodes if they are all unobstructed from current position.
    while (!current_path.IsOnGoalNode()) {
      Vector2f next = current_path.GetNext();

      CastResult cast = map.CastShip(self, radius, next);
      if (cast.hit) {
        break;
      }

      // Reset the stuck counter when we successfully move to a new node.
      ctx.blackboard.Set("bounce_count", 0);
      movement_target = current_path.Advance();
    }

    if (current_path.IsDone()) {
      current_path.Clear();
    }

    if (current_path.IsOnGoalNode() && current_path.GetCurrent().DistanceSq(self->position) < 2 * 2) {
      current_path.Clear();
    }

    auto& steering = ctx.bot->bot_controller->steering;
    bool is_stuck = IsStuck(*self, ctx);

    if (is_stuck) {
      // Calculate the corners of the ship.
      Vector2f corners[] = {
          self->position + Vector2f(-radius, -radius),
          self->position + Vector2f(radius, -radius),
          self->position + Vector2f(-radius, radius),
          self->position + Vector2f(radius, radius),
      };
      float best_dist_sq = 1024 * 1024;
      size_t best_corner = 0;

      // Find the closest corner to the movement target.
      for (size_t i = 0; i < ZERO_ARRAY_SIZE(corners); ++i) {
        float dist_sq = movement_target.DistanceSq(corners[i]);
        if (dist_sq < best_dist_sq) {
          best_dist_sq = dist_sq;
          best_corner = i;
        }
      }

      // Use the closest corner as our new target movement, so we rotate toward our closest corner.
      Vector2f new_target = corners[best_corner];

      Log(LogLevel::Debug, "Unstucking");

      steering.Face(game, new_target);
      // Avoid using seek because it wants to correct for velocity.
      steering.force += new_target - self->position;
    } else {
      float speed_sq = self->velocity.LengthSq();

      steering.Arrive(game, movement_target, 7.0f, 0.15f);
      if (speed_sq > 8.0f * 8.0f) {
        steering.AvoidWalls(game);
      }
    }

    return ExecuteResult::Success;
  }

 private:
  // Determine if we are stuck by checking last wall collision ticks and having a counter.
  static bool IsStuck(const Player& self, ExecuteContext& ctx) {
    // How many ticks of bouncing against a wall before it's considered stuck.
    constexpr u32 kStuckTickThreshold = 50;
    // The max number of bounces stored, which are depleted when not bouncing against a wall to eventually become
    // unstuck when below kStickTickThreshold again.
    constexpr u32 kStuckTickMax = 100;
    // How many ticks we are allowed to be in the stuck state.
    constexpr u32 kStuckTickMaxDuration = 400;

    u32 last_stored_bounce_tick = ctx.blackboard.ValueOr<u32>("last_bounce_tick", 0);
    u32 bounce_count = ctx.blackboard.ValueOr<u32>("bounce_count", 0);
    u32 previous_bounce_count = bounce_count;

    if (last_stored_bounce_tick != self.last_bounce_tick) {
      ++bounce_count;

      ctx.blackboard.Set("bounce_count", bounce_count);
      ctx.blackboard.Set("last_bounce_tick", self.last_bounce_tick);
    } else {
      if (bounce_count > 0) --bounce_count;
      if (bounce_count > kStuckTickMax) bounce_count = kStuckTickMax;

      ctx.blackboard.Set("bounce_count", bounce_count);
    }

    if (bounce_count >= kStuckTickThreshold) {
      Tick current_tick = GetCurrentTick();

      // Set the start tick so we can remove stuck status if it lasts too long.
      if (previous_bounce_count < kStuckTickThreshold) {
        ctx.blackboard.Set("stuck_start_tick", current_tick);
      }

      Tick start_tick = ctx.blackboard.ValueOr("stuck_start_tick", 0U);

      if (TICK_DIFF(current_tick, start_tick) >= kStuckTickMaxDuration) {
        ctx.blackboard.Set("bounce_count", 0);
        Log(LogLevel::Debug, "Unstuck for more than %u ticks.", kStuckTickMaxDuration);
        return false;
      }
    }

    return bounce_count >= kStuckTickThreshold;
  }

  Vector2f position;
  const char* position_key;
};

struct PathDistanceQueryNode : public BehaviorNode {
  PathDistanceQueryNode(const char* output_key) : output_key(output_key) {}
  PathDistanceQueryNode(const char* path_key, const char* output_key) : path_key(path_key), output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    path::Path path;

    if (path_key) {
      auto opt_path = ctx.blackboard.Value<path::Path>(path_key);
      if (!opt_path) return ExecuteResult::Failure;
      path = *opt_path;
    } else {
      path = ctx.bot->bot_controller->current_path;
    }

    float distance = 0.0f;

    if (!path.Empty() && path.index < path.points.size() - 1) {
      Vector2f previous = ctx.bot->game->player_manager.GetSelf()->position;

      for (size_t i = path.index; i < path.points.size(); ++i) {
        Vector2f current = path.points[i];

        distance += current.Distance(previous);
        previous = current;
      }
    }

    ctx.blackboard.Set<float>(output_key, distance);

    return ExecuteResult::Success;
  }

  const char* path_key = nullptr;
  const char* output_key = nullptr;
};

}  // namespace behavior
}  // namespace zero
