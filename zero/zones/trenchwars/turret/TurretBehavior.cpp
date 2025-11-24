#include "TurretBehavior.h"

#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/AttachNode.h>
#include <zero/behavior/nodes/InputActionNode.h>
#include <zero/behavior/nodes/MapNode.h>
#include <zero/behavior/nodes/MathNode.h>
#include <zero/behavior/nodes/MoveNode.h>
#include <zero/behavior/nodes/PlayerNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/behavior/nodes/TimerNode.h>
#include <zero/behavior/nodes/WaypointNode.h>
#include <zero/zones/trenchwars/nodes/AttachNode.h>

namespace zero {
namespace tw {

std::unique_ptr<behavior::BehaviorNode> TurretBehavior::CreateTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Selector()
        .Sequence() // Enter the specified ship if not already in it.
            .InvertChild<ShipQueryNode>("request_ship")
            .Child<ShipRequestNode>("request_ship")
            .End()
        .Sequence() // Attach to terrier if we can
            .InvertChild<ShipQueryNode>(4)
            .InvertChild<AttachedQueryNode>()
            .Selector() // Make the parent sequence return true when we are waiting for full energy.
                .InvertChild<PlayerEnergyPercentThresholdNode>(1.0f) // This is inverted so the parent sequence halts while waiting.
                .Sequence() // Try to attach once we have full energy.
                    .Child<TimerExpiredNode>("attach_cooldown")
                    .Child<BestAttachQueryNode>(false, "best_attach_player")
                    .Child<PlayerEnergyQueryNode>("best_attach_player", "best_attach_player_energy")
                    .Child<ScalarThresholdNode<float>>("best_attach_player_energy", 0.0f)
                    .Child<AttachNode>("best_attach_player")
                    .Child<TimerSetNode>("attach_cooldown", 100)
                    .End()
                .End()
            .End()
        .Sequence() // If we are in spec, do nothing
            .Child<ShipQueryNode>(8)
            .End()
        .Sequence()
            .Child<ShipQueryNode>(0)
            .Composite(CreateTurretGunnerBehavior(ctx))
            .End()
        .Sequence() // TODO: Jav
            .Child<ShipQueryNode>(1)
            .Composite(CreateTurretBomberBehavior(ctx))
            .End()
        .Sequence() // TODO: Spider
            .Child<ShipQueryNode>(2)
            .Composite(CreateTurretGunnerBehavior(ctx))
            .End()
        .Sequence() // TODO: Levi
            .Child<ShipQueryNode>(3)
            .Composite(CreateTurretBomberBehavior(ctx))
            .End()
        .Sequence() // TODO: Terrier
            .Child<ShipQueryNode>(4)
            .Composite(CreateTurretCarrierBehavior(ctx))
            .End()
        .Sequence() // TODO: Weasel
            .Child<ShipQueryNode>(5)
            .Composite(CreateTurretGunnerBehavior(ctx))
            .End()
        .Sequence() // TODO: Lancaster
            .Child<ShipQueryNode>(6)
            .Composite(CreateTurretGunnerBehavior(ctx))
            .End()
        .Sequence() // TODO: Shark
            .Child<ShipQueryNode>(7)
            .Composite(CreateTurretBomberBehavior(ctx))
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
