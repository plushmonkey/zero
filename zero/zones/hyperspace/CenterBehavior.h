#pragma once

#include <zero/Math.h>
#include <zero/behavior/Behavior.h>
#include <zero/behavior/BehaviorBuilder.h>

namespace zero {
namespace hyperspace {

struct CenterBehavior : public behavior::Behavior {
  void OnInitialize(behavior::ExecuteContext& ctx) override {
    // Setup blackboard here for this specific behavior
    ctx.blackboard.Set("request_ship", 0);
    ctx.blackboard.Set("leash_distance", 15.0f);

    std::vector<Vector2f> waypoints{
        Vector2f(440, 460),
        Vector2f(565, 465),
        Vector2f(570, 565),
        Vector2f(415, 590),
    };

    ctx.blackboard.Set("waypoints", waypoints);
  }

  std::unique_ptr<behavior::BehaviorNode> CreateTree(behavior::ExecuteContext& ctx) override;
};

}  // namespace hyperspace
}  // namespace zero
