#pragma once

#include <zero/Math.h>
#include <zero/behavior/Behavior.h>
#include <zero/behavior/BehaviorBuilder.h>

namespace zero {
namespace eg {

struct CenterBehavior : public behavior::Behavior {
  void OnInitialize(behavior::ExecuteContext& ctx) override {
    // Setup blackboard here for this specific behavior
    ctx.blackboard.Set("request_ship", 1);
    ctx.blackboard.Set("leash_distance", 30.0f);

    std::vector<Vector2f> waypoints{
        Vector2f(415, 480),
        Vector2f(595, 480),
        Vector2f(585, 600),
        Vector2f(425, 600),
    };

    ctx.blackboard.Set("waypoints", waypoints);
  }

  std::unique_ptr<behavior::BehaviorNode> CreateTree(behavior::ExecuteContext& ctx) override;
};

}  // namespace mg
}  // namespace zero
