#include "TeamBehavior.h"

#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/InputActionNode.h>
#include <zero/behavior/nodes/MapNode.h>
#include <zero/behavior/nodes/MathNode.h>
#include <zero/behavior/nodes/MoveNode.h>
#include <zero/behavior/nodes/PlayerNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/behavior/nodes/TimerNode.h>
#include <zero/behavior/nodes/WaypointNode.h>
#include <zero/zones/trenchwars/solo/SoloWarbirdBehavior.h>

namespace zero {
namespace tw {

std::unique_ptr<behavior::BehaviorNode> TeamBehavior::CreateTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  // clang-format off

  builder
    .Selector()
        .Sequence() // Enter the specified ship if not already in it.
            .InvertChild<ShipQueryNode>("request_ship")
            .Child<ShipRequestNode>("request_ship")
            .End()
        .Sequence() // Join requested freq
            .Child<PlayerFrequencyQueryNode>("self_freq")
            .InvertChild<EqualityNode<u16>>("self_freq", "request_freq")
            .Child<TimerExpiredNode>("next_freq_change_tick")
            .Child<TimerSetNode>("next_freq_change_tick", 300)
            .Child<PlayerChangeFrequencyNode>("request_freq")
            .End()
        .Sequence() // If we are in spec, do nothing
            .Child<ShipQueryNode>(8)
            .End()
        .Sequence()
            .Child<ShipQueryNode>(0)
            .Composite(CreateSoloWarbirdTree(ctx))
            .End()
        .Sequence() // TODO: Jav
            .Child<ShipQueryNode>(1)
            .Composite(CreateSoloWarbirdTree(ctx))
            .End()
        .Sequence() // TODO: Spider
            .Child<ShipQueryNode>(2)
            .Composite(CreateSoloWarbirdTree(ctx))
            .End()
        .Sequence() // TODO: Levi
            .Child<ShipQueryNode>(3)
            .Composite(CreateSoloWarbirdTree(ctx))
            .End()
        .Sequence() // TODO: Terrier
            .Child<ShipQueryNode>(4)
            .Composite(CreateSoloWarbirdTree(ctx))
            .End()
        .Sequence() // TODO: Weasel
            .Child<ShipQueryNode>(5)
            .Composite(CreateSoloWarbirdTree(ctx))
            .End()
        .Sequence() // TODO: Lancaster
            .Child<ShipQueryNode>(6)
            .Composite(CreateSoloWarbirdTree(ctx))
            .End()
        .Sequence() // TODO: Shark
            .Child<ShipQueryNode>(7)
            .Composite(CreateSoloWarbirdTree(ctx))
            .End()
        .Sequence() // Follow set waypoints.
            .Child<WaypointNode>("waypoints", "waypoint_index", "waypoint_position", 15.0f)
            .Selector()
                .Sequence()
                    .Child<ShipTraverseQueryNode>("waypoint_position")
                    .Child<FaceNode>("waypoint_position")
                    .Child<ArriveNode>("waypoint_position", 1.25f)
                    .End()
                .Sequence()
                    .Child<GoToNode>("waypoint_position")
                    .End()
                .End()
            .End()
        .Sequence() // Warp out if all above sequences failed. We must be in a non-traversable area because waypoint following failed to build a path.
            .Child<WarpNode>()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace tw
}  // namespace zero
