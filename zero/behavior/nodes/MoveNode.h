#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

struct SeekNode : public BehaviorNode {
  SeekNode(const char* position_key) : position_key(position_key), target_distance_key(nullptr) {}
  SeekNode(const char* position_key, const char* target_distance_key)
      : position_key(position_key), target_distance_key(target_distance_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
    if (!opt_position.has_value()) return ExecuteResult::Failure;

    Vector2f position = opt_position.value();

    float target_distance = 0.0f;

    if (target_distance_key) {
      auto opt_distance = ctx.blackboard.Value<float>(target_distance_key);

      if (opt_distance.has_value()) {
        target_distance = opt_distance.value();
      }
    }

    if (target_distance > 0.0f) {
      ctx.bot->bot_controller->steering.Seek(*ctx.bot->game, position, target_distance);
    } else {
      ctx.bot->bot_controller->steering.Seek(*ctx.bot->game, position);
    }

    return ExecuteResult::Success;
  }

  const char* position_key;
  const char* target_distance_key;
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

        if (!cast.hit) {
          build = false;
        }
      }
    }

    if (build) {
      current_path = pathfinder->FindPath(game.connection.map, self->position, target, radius);
    }

    if (current_path.IsDone()) {
      return ExecuteResult::Success;
    }

    if (current_path.Empty()) {
      return ExecuteResult::Failure;
    }

    Vector2f movement_target = current_path.GetCurrent();

    if (current_path.IsCurrentTile(self->position)) {
      movement_target = current_path.Advance();
    }

    // Cull future nodes if they are all unobstructed from current position.
    while (!current_path.IsOnGoalNode()) {
      Vector2f next = current_path.GetNext();

      if (!CanMoveBetween(game, self->position, next, radius, self->frequency)) {
        break;
      }

      movement_target = current_path.Advance();
    }

    if (current_path.IsDone()) {
      current_path.Clear();
    }

    if (current_path.IsOnGoalNode() && current_path.GetCurrent().DistanceSq(self->position) < 2 * 2) {
      current_path.Clear();
    }

    float speed_sq = self->velocity.LengthSq();
    auto& steering = ctx.bot->bot_controller->steering;
    if (speed_sq < 0.3f * 0.3f) {
      Vector2f direction = Normalize(movement_target - self->position);
      Vector2f new_direction;

      if (fabsf(direction.x) < fabsf(direction.y)) {
        new_direction = Normalize(Reflect(direction, Vector2f(0, 1)));
      } else {
        new_direction = Normalize(Reflect(direction, Vector2f(1, 0)));
      }

      // Face a reflected vector so it rotates away from the wall.
      steering.Face(game, self->position + new_direction);
      steering.Seek(game, movement_target);
    } else {
      if (speed_sq > 1.0f * 1.0f) {
        // Arrive to node when moving fast enough.
        steering.Arrive(game, movement_target, 0.25f);
      } else {
        // Seek when going slow so it can get around corners.
        steering.Seek(game, movement_target);
      }

      // Avoid walls when moving fast so it slows down while approaching a wall.
      if (speed_sq > 8.0f * 8.0f) {
        steering.AvoidWalls(game);
      }
    }

    return ExecuteResult::Success;
  }

 private:
  bool CanMoveBetween(Game& game, Vector2f from, Vector2f to, float radius, u32 frequency) {
    Vector2f trajectory = to - from;
    Vector2f direction = Normalize(trajectory);
    Vector2f side = Perpendicular(direction);

    float distance = from.Distance(to);

    CastResult center = game.GetMap().Cast(from, direction, distance, frequency);
    CastResult side1 = game.GetMap().Cast(from + side * radius, direction, distance, frequency);
    CastResult side2 = game.GetMap().Cast(from - side * radius, direction, distance, frequency);

    return !center.hit && !side1.hit && !side2.hit;
  }

  Vector2f position;
  const char* position_key;
};

}  // namespace behavior
}  // namespace zero
