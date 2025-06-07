#include "BasingBehavior.h"

#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/MapNode.h>
#include <zero/behavior/nodes/MoveNode.h>
#include <zero/behavior/nodes/RenderNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/behavior/nodes/WaypointNode.h>
#include <zero/zones/trenchwars/TrenchWars.h>

namespace zero {
namespace tw {

std::unique_ptr<behavior::BehaviorNode> BasingBehavior::CreateTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Selector()
        .Sequence() // Enter the specified ship if not already in it.
            .InvertChild<ShipQueryNode>("request_ship")
            .Child<ShipRequestNode>("request_ship")
            .End()
        .Sequence() // TODO: Warbird
            .Child<ShipQueryNode>(0)
            .Composite(CreateSpiderBasingTree(ctx))
            .End()
        .Sequence() // TODO: Jav
            .Child<ShipQueryNode>(1)
            .Composite(CreateJavelinBasingTree(ctx))
            .End()
        .Sequence() // Spider
            .Child<ShipQueryNode>(2)
            .Composite(CreateSpiderBasingTree(ctx))
            .End()
        .Sequence() // TODO: Levi
            .Child<ShipQueryNode>(3)
            .Composite(CreateSharkBasingTree(ctx))
            .End()
        .Sequence() // Terrier
            .Child<ShipQueryNode>(4)
            .Composite(CreateTerrierBasingTree(ctx))
            .End()
        .Sequence() // TODO: Weasel
            .Child<ShipQueryNode>(5)
            .Composite(CreateSpiderBasingTree(ctx))
            .End()
        .Sequence() // Lancaster
            .Child<ShipQueryNode>(6)
            .Composite(CreateSpiderBasingTree(ctx))
            .End()
        .Sequence() // Shark
            .Child<ShipQueryNode>(7)
            .Composite(CreateSharkBasingTree(ctx))
            .End()
        .Sequence() // If we are in spec, do nothing
            .Child<ShipQueryNode>(8)
            .End()
        .Sequence() // Follow set waypoints.
            .Child<WaypointNode>("waypoints", "waypoint_index", "waypoint_position", 15.0f)
            .Selector()
                .Sequence()
                    .InvertChild<ShipTraverseQueryNode>("waypoint_position")
                    .Child<GoToNode>("waypoint_position")
                    .Child<RenderPathNode>(Vector3f(0.0f, 0.5f, 1.0f))
                    .End()
                .Parallel()
                    .Child<FaceNode>("waypoint_position")
                    .Child<ArriveNode>("waypoint_position", 1.25f)
                    .End()
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace tw
}  // namespace zero
