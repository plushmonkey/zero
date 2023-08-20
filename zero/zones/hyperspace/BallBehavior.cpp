#include "BallBehavior.h"

#include <zero/behavior/nodes/InputActionNode.h>
#include <zero/behavior/nodes/MapNode.h>
#include <zero/behavior/nodes/MathNode.h>
#include <zero/behavior/nodes/MoveNode.h>
#include <zero/behavior/nodes/PowerballNode.h>
#include <zero/behavior/nodes/RegionNode.h>
#include <zero/behavior/nodes/ShipNode.h>

namespace zero {
namespace hyperspace {

std::unique_ptr<behavior::BehaviorNode> BallBehavior::CreateTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  const Vector2f center(512, 512);

  // clang-format off
   builder
    .Selector()
        .Sequence() // Enter the specified ship if not already in it.
            .InvertChild<ShipQueryNode>("request_ship")
            .Child<ShipRequestNode>("request_ship")
            .End()
        .Sequence() // Warp back to center.
            .InvertChild<RegionContainQueryNode>(center)
            .Child<WarpNode>()
            .End()
        .Sequence() // If the ball is not being carried, path to nearest ball
            .InvertChild<PowerballCarryQueryNode>()
            .Child<PowerballClosestQueryNode>("closest_powerball_position")
            .Child<GoToNode>("closest_powerball_position")
            .End()
        .Sequence()
            .Child<PowerballCarryQueryNode>()
            .Parallel()
                .Sequence()
                    .Child<PowerballGoalPathQuery>()
                    .Child<PowerballFireNode>()
                    .End()
                .Selector() // If the ball is being carried, go toward the goal
                    .Sequence() // If the ball is low on timer, fire it.
                        .Child<PowerballRemainingTimeQueryNode>("powerball_remaining_time")
                        .InvertChild<ScalarThresholdNode<float>>("powerball_remaining_time", 0.2f)
                        .Child<PowerballFireNode>()
                        .End()
                    .Sequence()
                        .Child<GoToNode>("goal_position")
                        .End()
                    .End()
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace hyperspace
}  // namespace zero
