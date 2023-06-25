#pragma once

#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

struct WaypointNode : public BehaviorNode {
  WaypointNode(const char* waypoints_key, const char* index_key, const char* position_key, float nearby_radius)
      : waypoints_key(waypoints_key),
        index_key(index_key),
        position_key(position_key),
        nearby_radius_sq(nearby_radius * nearby_radius) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return ExecuteResult::Failure;

    auto opt_waypoints = ctx.blackboard.Value<std::vector<Vector2f>>(waypoints_key);
    if (!opt_waypoints.has_value()) return ExecuteResult::Failure;

    std::vector<Vector2f>& waypoints = opt_waypoints.value();
    if (waypoints.empty()) return ExecuteResult::Failure;

    size_t index = ctx.blackboard.ValueOr<size_t>(index_key, 0);

    if (index >= waypoints.size()) {
      index = 0;
    }

    Vector2f& target = waypoints[index];

    if (self->position.DistanceSq(target) <= nearby_radius_sq) {
      index = (index + 1) % waypoints.size();
      target = waypoints[index];
    }

    ctx.blackboard.Set<Vector2f>(position_key, target);
    ctx.blackboard.Set(index_key, index);

    return ExecuteResult::Success;
  }

  const char* waypoints_key;
  const char* index_key;
  const char* position_key;
  float nearby_radius_sq;
};

}  // namespace behavior
}  // namespace zero
