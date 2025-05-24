#pragma once

#include <zero/Math.h>
#include <zero/behavior/Behavior.h>
#include <zero/behavior/BehaviorBuilder.h>

namespace zero {
namespace tw {

struct SoloBehavior : public behavior::Behavior {
  void OnInitialize(behavior::ExecuteContext& ctx) override {
    // Setup blackboard here for this specific behavior
    ctx.blackboard.Set("request_ship", 0);
    ctx.blackboard.Set("leash_distance", 30.0f);

    std::vector<Vector2f> waypoints{
        Vector2f(435, 425),
        Vector2f(589, 425),
        Vector2f(512, 512),
    };

    ctx.blackboard.Set("waypoints", waypoints);
  }

  std::unique_ptr<behavior::BehaviorNode> CreateTree(behavior::ExecuteContext& ctx) override;
};

}  // namespace tw
}  // namespace zero
