#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>
#include <zero/game/Logger.h>

namespace zero {
namespace behavior {

struct PursueNode : public BehaviorNode {
  PursueNode(const char* position_key, const char* target_player_key, const char* target_distance_key)
      : position_key(position_key), target_player_key(target_player_key), target_distance_key(target_distance_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
    if (!opt_position.has_value()) return ExecuteResult::Failure;

    Vector2f position = *opt_position;

    auto opt_target_distance = ctx.blackboard.Value<float>(target_distance_key);
    if (!opt_target_distance) return ExecuteResult::Failure;

    float target_distance = *opt_target_distance;

    auto opt_target = ctx.blackboard.Value<Player*>(target_player_key);
    if (!opt_target) return ExecuteResult::Failure;

    Player* target = *opt_target;

    if (target_distance > 0.0f) {
      auto self = ctx.bot->game->player_manager.GetSelf();
      Vector2f to_target = position - self->position;

      ctx.bot->bot_controller->steering.Pursue(*ctx.bot->game, position, *target, target_distance);
    } else {
      ctx.bot->bot_controller->steering.Pursue(*ctx.bot->game, position, *target, 100000.0f);
    }

    return ExecuteResult::Success;
  }

  const char* position_key = nullptr;
  const char* target_distance_key = nullptr;
  const char* target_player_key = nullptr;
};

struct SeekNode : public BehaviorNode {
  enum class DistanceResolveType { Static, Zero, Dynamic };
  SeekNode(const char* position_key) : position_key(position_key) {}
  SeekNode(const char* position_key, float target_distance, DistanceResolveType type)
      : position_key(position_key), target_distance(target_distance), distance_type(type) {}
  SeekNode(const char* position_key, const char* target_distance_key)
      : position_key(position_key), target_distance_key(target_distance_key) {}
  SeekNode(const char* position_key, const char* target_distance_key, DistanceResolveType type)
      : position_key(position_key), target_distance_key(target_distance_key), distance_type(type) {}

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

struct AvoidTeamNode : public BehaviorNode {
  AvoidTeamNode(const char* dist_key) : dist_key(dist_key) {}
  AvoidTeamNode(float dist) : dist(dist) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    if (dist_key) {
      auto opt_dist = ctx.blackboard.Value<float>(dist_key);
      if (!opt_dist) return ExecuteResult::Failure;
      dist = *opt_dist;
    }

    ctx.bot->bot_controller->steering.AvoidTeam(ctx.bot->bot_controller->game, dist);
    return ExecuteResult::Success;
  }

  float dist = 0.0f;
  const char* dist_key = nullptr;
};

struct AvoidEnemyNode : public BehaviorNode {
  AvoidEnemyNode(const char* dist_key) : dist_key(dist_key) {}
  AvoidEnemyNode(float dist) : dist(dist) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    if (dist_key) {
      auto opt_dist = ctx.blackboard.Value<float>(dist_key);
      if (!opt_dist) return ExecuteResult::Failure;
      dist = *opt_dist;
    }

    ctx.bot->bot_controller->steering.AvoidEnemy(ctx.bot->bot_controller->game, dist);
    return ExecuteResult::Success;
  }

  float dist = 0.0f;
  const char* dist_key = nullptr;
};

struct AvoidWallsNode : public BehaviorNode {
  ExecuteResult Execute(ExecuteContext& ctx) override {
    ctx.bot->bot_controller->steering.AvoidWalls(ctx.bot->bot_controller->game);
    return ExecuteResult::Success;
  }
};

struct RotationThresholdSetNode : public BehaviorNode {
  RotationThresholdSetNode(const char* threshold_key) : threshold_key(threshold_key) {}
  RotationThresholdSetNode(float threshold) : threshold(threshold) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    float threshold = this->threshold;

    if (threshold_key) {
      auto opt_threshold = ctx.blackboard.Value<float>(threshold_key);

      if (!opt_threshold) return ExecuteResult::Failure;

      threshold = *opt_threshold;
    }

    ctx.bot->bot_controller->steering.SetRotationThreshold(threshold);

    return ExecuteResult::Success;
  }

  float threshold = 0.0f;
  const char* threshold_key = nullptr;
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

// Follows the 'current_path' from the bot controller without rebuilding.
struct FollowPathNode : public BehaviorNode {
  ExecuteResult Execute(ExecuteContext& ctx) override;

 private:
  // Determine if we are stuck by checking last wall collision ticks and having a counter.
  static bool IsStuck(const Player& self, ExecuteContext& ctx);
};

// Generic movement node that will rebuild the path when necessary.
// The generated path will be stored in the bot controller's 'current_path' variable.
struct GoToNode : public BehaviorNode {
  GoToNode(const char* position_key) : position_key(position_key) {}
  GoToNode(Vector2f position) : position(position), position_key(nullptr) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return ExecuteResult::Failure;

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
      // Try to find a new path, but continue to use the old one if we can't find a new one.
      auto new_path = pathfinder->FindPath(game.connection.map, self->position, target, radius, self->frequency);

      if (!new_path.Empty()) {
        current_path = new_path;
      }

      if (current_path.points.size() > 10) {
        Log(LogLevel::Jabber, "Rebuilding path");
      }
    }

    return follow_node.Execute(ctx);
  }

 private:
  FollowPathNode follow_node;

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

struct ActuatorReverseNode : public BehaviorNode {
  ActuatorReverseNode(bool allow) : allow_reversing(allow) {}
  ActuatorReverseNode(const char* allow_key) : allow_key(allow_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    bool allow = this->allow_reversing;

    if (allow_key) {
      auto opt_allow = ctx.blackboard.Value<bool>(allow_key);
      if (!opt_allow) return ExecuteResult::Failure;

      allow = *opt_allow;
    }

    ctx.bot->bot_controller->actuator.allow_reversing = allow;

    return ExecuteResult::Success;
  }

  bool allow_reversing = true;
  const char* allow_key = nullptr;
};

struct ActuatorForwardVectorRequirementNode : public BehaviorNode {
  ActuatorForwardVectorRequirementNode(float percent) : percent(percent) {}
  ActuatorForwardVectorRequirementNode(const char* percent_key) : percent_key(percent_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    float required_percent = this->percent;

    if (percent_key) {
      auto opt_percent = ctx.blackboard.Value<bool>(percent_key);
      if (!opt_percent) return ExecuteResult::Failure;

      required_percent = *opt_percent;
    }

    ctx.bot->bot_controller->actuator.required_forward_vector_to_thrust = required_percent;

    return ExecuteResult::Success;
  }

  float percent = 0.65f;
  const char* percent_key = nullptr;
};

}  // namespace behavior
}  // namespace zero
