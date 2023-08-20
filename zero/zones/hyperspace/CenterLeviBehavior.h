#pragma once

#include <zero/Math.h>
#include <zero/behavior/Behavior.h>
#include <zero/behavior/BehaviorBuilder.h>

namespace zero {
namespace hyperspace {

struct CenterLeviBehavior : public behavior::Behavior {
  void OnInitialize(behavior::ExecuteContext& ctx) override {
    // Setup blackboard here for this specific behavior
    ctx.blackboard.Set("request_ship", 3);
    ctx.blackboard.Set("leash_distance", 15.0f);

    std::vector<Vector2f> waypoints{
        Vector2f(440, 460),
        Vector2f(565, 465),
        Vector2f(570, 565),
        Vector2f(415, 590),
    };

    ctx.blackboard.Set("waypoints", waypoints);

    std::vector<Vector2f> center_warpers{
        Vector2f(500, 500),
        Vector2f(523, 500),
        Vector2f(523, 523),
        Vector2f(500, 523),
    };

    ctx.blackboard.Set("center_warpers", center_warpers);

    std::vector<Vector2f> levi_camp_points{
        Vector2f(630, 475),
        Vector2f(600, 600),
        Vector2f(420, 605),
        Vector2f(410, 450),
    };

    ctx.blackboard.Set("levi_camp_points", levi_camp_points);

    std::vector<Vector2f> levi_aim_points{
        Vector2f(512, 480),  // North
        Vector2f(543, 511),  // East
        Vector2f(512, 543),  // South
        Vector2f(480, 511),  // West
    };

    ctx.blackboard.Set("levi_aim_points", levi_aim_points);
  }

  std::unique_ptr<behavior::BehaviorNode> CreateTree(behavior::ExecuteContext& ctx) override;
};

}  // namespace hyperspace
}  // namespace zero
