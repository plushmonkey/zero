#pragma once

#include <zero/Math.h>
#include <zero/behavior/Behavior.h>
#include <zero/behavior/BehaviorBuilder.h>

namespace zero {
namespace tw {

struct BasingBehavior : public behavior::Behavior {
  virtual void OnInitialize(behavior::ExecuteContext& ctx) override {
    // Setup blackboard here for this specific behavior

    // Only set request ship if we don't already have one.
    // This lets us retain our ship when switching between behaviors.
    if (!ctx.blackboard.Has("request_ship")) {
      ctx.blackboard.Set("request_ship", 2);
    }

    ctx.blackboard.Set("leash_distance", 35.0f);

    std::vector<Vector2f> waypoints{
        Vector2f(495, 275),
        Vector2f(505, 263),
        Vector2f(530, 265),
        Vector2f(525, 280),
    };

    ctx.blackboard.Set("waypoints", waypoints);
  }

  std::unique_ptr<behavior::BehaviorNode> CreateTree(behavior::ExecuteContext& ctx) override;
};

}  // namespace tw
}  // namespace zero
