#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

struct SeekNode : public BehaviorNode {
  SeekNode(const char* position_key, const char* target_distance_key)
      : position_key(position_key), target_distance_key(target_distance_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
    if (!opt_position.has_value()) return ExecuteResult::Failure;

    Vector2f position = opt_position.value();

    auto opt_distance = ctx.blackboard.Value<float>(target_distance_key);
    if (opt_distance.has_value()) {
      float target_distance = opt_distance.value();

      ctx.bot->bot_controller->steering.Seek(*ctx.bot->game, position, target_distance);
    } else {
      ctx.bot->bot_controller->steering.Seek(*ctx.bot->game, position);
    }

    return ExecuteResult::Success;
  }

  const char* position_key;
  const char* target_distance_key;
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

  ExecuteResult Execute(ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return ExecuteResult::Failure;

    auto opt_pos = ctx.blackboard.Value<Vector2f>(position_key);
    if (!opt_pos.has_value()) return ExecuteResult::Failure;

    Vector2f& target = opt_pos.value();
    auto& map = ctx.bot->game->GetMap();

    CastResult cast = map.CastTo(self->position, target, self->frequency);

    if (!cast.hit) {
      ctx.bot->bot_controller->steering.Seek(*ctx.bot->game, target);

      return ExecuteResult::Success;
    }

    auto& current_path = ctx.bot->bot_controller->current_path;
    auto& pathfinder = ctx.bot->bot_controller->pathfinder;
    auto& game = *ctx.bot->game;
    bool build = true;

    float radius = game.connection.settings.ShipSettings[self->ship].GetRadius();

    if (!current_path.empty()) {
      if (target.DistanceSq(current_path.back()) <= 3.0f * 3.0f) {
        Vector2f next = current_path.front();
        Vector2f direction = Normalize(next - self->position);
        Vector2f side = Perpendicular(direction);
        float distance = next.Distance(self->position);

        CastResult center = game.GetMap().Cast(self->position, direction, distance, self->frequency);
        CastResult side1 = game.GetMap().Cast(self->position + side * radius, direction, distance, self->frequency);
        CastResult side2 = game.GetMap().Cast(self->position - side * radius, direction, distance, self->frequency);

        if (!center.hit && !side1.hit && !side2.hit) {
          build = false;
        }
      }
    }

    if (build) {
      current_path = pathfinder->FindPath(game.connection.map, self->position, target, radius);
      current_path = pathfinder->SmoothPath(game, current_path, radius);
    }

    if (current_path.empty()) {
      return ExecuteResult::Failure;
    }

    Vector2f movement_target = current_path.front();

    if (!current_path.empty() && (u16)self->position.x == (u16)current_path.at(0).x &&
        (u16)self->position.y == (u16)current_path.at(0).y) {
      current_path.erase(current_path.begin());

      if (!current_path.empty()) {
        movement_target = current_path.front();
      }
    }

    // Cull future nodes if they are all unobstructed from current position.
    while (current_path.size() > 1 &&
           CanMoveBetween(game, self->position, current_path.at(1), radius, self->frequency)) {
      current_path.erase(current_path.begin());
      movement_target = current_path.front();
    }

    if (current_path.size() == 1 && current_path.front().DistanceSq(self->position) < 2 * 2) {
      current_path.clear();
    }

    ctx.bot->bot_controller->steering.Seek(game, movement_target);
    ctx.bot->bot_controller->steering.AvoidWalls(game, 30.0f);

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

  const char* position_key;
};

}  // namespace behavior
}  // namespace zero
