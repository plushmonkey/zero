#pragma once

#include <zero/Math.h>
#include <zero/behavior/Behavior.h>
#include <zero/behavior/BehaviorBuilder.h>

namespace zero {
namespace deva {

struct CenterBehavior : public behavior::Behavior {
  void OnInitialize(behavior::ExecuteContext& ctx) override {
    // Setup blackboard here for this specific behavior
    ctx.blackboard.Set("request_ship", 1);
    ctx.blackboard.Set("leash_distance", 15.0f);

    std::vector<Vector2f> waypoints{
        Vector2f(455, 455),
        Vector2f(570, 455),
        Vector2f(560, 570),
        Vector2f(455, 570),
    };

    ctx.blackboard.Set("waypoints", waypoints);
  }

  std::unique_ptr<behavior::BehaviorNode> CreateTree(behavior::ExecuteContext& ctx) override;
};

}  // namespace deva
}  // namespace zero
