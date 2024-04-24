#include "CenterBehavior.h"

#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/AimNode.h>
#include <zero/behavior/nodes/BlackboardNode.h>
#include <zero/behavior/nodes/InputActionNode.h>
#include <zero/behavior/nodes/MapNode.h>
#include <zero/behavior/nodes/MathNode.h>
#include <zero/behavior/nodes/MoveNode.h>
#include <zero/behavior/nodes/PlayerNode.h>
#include <zero/behavior/nodes/PowerballNode.h>
#include <zero/behavior/nodes/RegionNode.h>
#include <zero/behavior/nodes/RenderNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/behavior/nodes/TargetNode.h>
#include <zero/behavior/nodes/ThreatNode.h>
#include <zero/behavior/nodes/TimerNode.h>
#include <zero/behavior/nodes/WaypointNode.h>
#include <zero/zones/svs/nodes/BurstAreaQueryNode.h>
#include <zero/zones/svs/nodes/IncomingDamageQueryNode.h>

namespace zero {
namespace deva {

std::unique_ptr<behavior::BehaviorNode> CreateJavTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  // clang-format off
  builder
      .Sequence() // Find nearest target and either path to them or seek them directly.
          .Sequence()
              .Child<PlayerPositionQueryNode>("self_position")
              .Child<NearestTargetNode>("nearest_target")
              .Child<PlayerPositionQueryNode>("nearest_target", "nearest_target_position")
              .End()
          .Selector()
              .Sequence() // Path to target if they aren't immediately visible.
                  .InvertChild<VisibilityQueryNode>("nearest_target_position")
                  .Child<GoToNode>("nearest_target_position")
                  .End()
              .Parallel()
                  .Sequence(CompositeDecorator::Success) // Always check incoming damage so we can use it in repel and portal sequences.
                      .Child<ShipItemCountThresholdNode>(ShipItemType::Repel)
                      .Child<TimerExpiredNode>("defense_timer")
                      .Child<svs::IncomingDamageQueryNode>(5.0f, "incoming_damage")
                      .Child<PlayerCurrentEnergyQueryNode>("self_energy")
                      .Child<ScalarThresholdNode<float>>("incoming_damage", "self_energy")
                      .Child<InputActionNode>(InputAction::Repel)
                      .Child<TimerSetNode>("defense_timer", 100)
                      .End()
                  .Sequence(CompositeDecorator::Success) // Use burst when near a wall.
                      .Child<ShipItemCountThresholdNode>(ShipItemType::Burst)
                      .InvertChild<ShipWeaponCooldownQueryNode>(WeaponType::Bomb)
                      .InvertChild<DistanceThresholdNode>("nearest_target_position", 15.0f)
                      .Child<svs::BurstAreaQueryNode>()
                      .Child<InputActionNode>(InputAction::Burst)
                      .End()
                  .Sequence() // Aim at target and shoot while seeking them.
                      .Child<AimNode>(WeaponType::Bullet, "nearest_target", "aimshot")
                      .Child<FaceNode>("aimshot")
                      .Selector()
                          .Sequence()
                              .Child<PlayerEnergyQueryNode>("nearest_target", "target_energy")
                              .Child<PlayerEnergyQueryNode>("self_energy")
                              .Child<GreaterOrEqualThanNode<float>>("self_energy", "target_energy")
                              .Child<SeekNode>("aimshot", "leash_distance")
                              .End()
                          .Sequence()
                              .Child<SeekNode>("aimshot", 25.0f, SeekNode::DistanceResolveType::Dynamic)
                              .End()
                          .End()
                      .Sequence(CompositeDecorator::Success) // Determine if a shot should be fired by using weapon trajectory and bounding boxes.
                          .InvertChild<TileQueryNode>(kTileSafeId)
                          .Child<ShotVelocityQueryNode>(WeaponType::Bullet, "bullet_fire_velocity")
                          .Child<RayNode>("self_position", "bullet_fire_velocity", "bullet_fire_ray")
                          .Child<PlayerBoundingBoxQueryNode>("nearest_target", "target_bounds", 3.0f)
                          .Child<MoveRectangleNode>("target_bounds", "aimshot", "target_bounds")
                          .Child<RenderRectNode>("world_camera", "target_bounds", Vector3f(1.0f, 0.0f, 0.0f))
                          .Child<RenderRayNode>("world_camera", "bullet_fire_ray", 50.0f, Vector3f(1.0f, 1.0f, 0.0f))
                          .Child<RayRectangleInterceptNode>("bullet_fire_ray", "target_bounds")
                          .Child<InputActionNode>(InputAction::Bullet)
                          .End()
                      .End()
                  .End()
              .End()
          .End();
  // clang-format on

    return builder.Build();
}

std::unique_ptr<behavior::BehaviorNode> CenterBehavior::CreateTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Selector()
        .Sequence() // Enter the specified ship if not already in it.
            .InvertChild<ShipQueryNode>("request_ship")
            .Child<ShipRequestNode>("request_ship")
            .End()
        .Selector() // Choose to fight the player or follow waypoints.
            .Sequence() // Execute jav center behavior
                .Child<ShipQueryNode>(1)
                .Composite(CreateJavTree(ctx))
                .End()
            .Sequence() // Follow set waypoints.
                .Child<DebugPrintNode>("Waypoint")
                .Child<WaypointNode>("waypoints", "waypoint_index", "waypoint_position", 15.0f)
                .Selector()
                    .Sequence()
                        .InvertChild<VisibilityQueryNode>("waypoint_position")
                        .Child<GoToNode>("waypoint_position")
                        .End()
                    .Parallel()
                        .Child<FaceNode>("waypoint_position")
                        .Child<ArriveNode>("waypoint_position", 1.25f)
                        .End()
                    .End()
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace deva
}  // namespace zero
