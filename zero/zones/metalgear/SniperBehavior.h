#pragma once

#include <zero/Math.h>
#include <zero/behavior/Behavior.h>
#include <zero/behavior/BehaviorBuilder.h>

namespace zero {
namespace mg {

struct SniperBehavior : public behavior::Behavior {
  void OnInitialize(behavior::ExecuteContext& ctx) override {
    // Setup blackboard here for this specific behavior
    ctx.blackboard.Set("request_ship", 6);
    ctx.blackboard.Set("leash_distance", 55.0f);

    std::vector<Vector2f> waypoints{
        Vector2f(445, 425),
        Vector2f(585, 425),
        Vector2f(585, 600),
        Vector2f(445, 600),
    };

    ctx.blackboard.Set("waypoints", waypoints);
  }

  std::unique_ptr<behavior::BehaviorNode> CreateTree(behavior::ExecuteContext& ctx) override;
};

}  // namespace mg
}  // namespace zero
