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
#include <zero/zones/svs/nodes/DynamicPlayerBoundingBoxQueryNode.h>
#include <zero/zones/svs/nodes/IncomingDamageQueryNode.h>

namespace zero {
namespace hyperspace {

// This tree will generate a territory position around the current target that it tries to stay near.
// It uses weapon trajectories to determine the threat to self and territory position.
// If it finds the territory is threatened enough, it will generate a new one around the target and path to it.
//
// Debug display:
//   The yellow line is the trajectory of self's potential bullet shot.
//   Green rect is the generated territory bounds.
//   Red rect is the current aimshot.
//   Self threat is rendered with the ui camera.
//   Territory threat is rendered at the territory.
std::unique_ptr<behavior::BehaviorNode> CenterBehavior::CreateTree(behavior::ExecuteContext& ctx) {
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
        .Selector() // Choose to fight the player or follow waypoints.
            .Sequence() // Find nearest target and either path to them or seek them directly.
                .Sequence()
                    .Child<PlayerPositionQueryNode>("self_position")
                    .Child<NearestTargetNode>("nearest_target")
                    .Child<PlayerPositionQueryNode>("nearest_target", "nearest_target_position")
                    .Child<AimNode>(WeaponType::Bullet, "nearest_target", "aimshot")
                    .End()
                .Selector()
                    .Sequence() // Always check incoming damage so we can use it in repel and portal sequences.
                        .Child<ShipItemCountThresholdNode>(ShipItemType::Repel)
                        .Child<TimerExpiredNode>("defense_timer")
                        .Child<svs::IncomingDamageQueryNode>(6.0f, "incoming_damage")
                        .Child<PlayerCurrentEnergyQueryNode>("self_energy")
                        .Child<ScalarThresholdNode<float>>("incoming_damage", "self_energy")
                        .Child<InputActionNode>(InputAction::Repel)
                        .Child<TimerSetNode>("defense_timer", 100)
                        .End()
                    .Sequence()
                        .Child<DodgeIncomingDamage>(0.2f, 35.0f)
                        .End()
                    .Sequence() // Path to target if they aren't immediately visible.
                        .InvertChild<VisibilityQueryNode>("nearest_target_position")
                        .Child<GoToNode>("nearest_target_position")
                        .End()
                    .Sequence() // Aim at target and shoot while seeking them.
                        .Parallel()
                            .Selector()
                                .Sequence() // If we have enough energy, rush at them.
                                    .Child<PlayerEnergyQueryNode>("nearest_target", "nearest_target_energy")
                                    .Child<PlayerEnergyQueryNode>("self_energy")
                                    .Child<GreaterThanNode<float>>("self_energy", "nearest_target_energy")
                                    .Child<FaceNode>("aimshot")
                                    .Child<SeekNode>("aimshot")
                                    .Child<ScalarNode>(1.0f, "aim_override")
                                    .End()
                                .Sequence()
                                    .Child<FaceNode>("aimshot")
                                    .Child<SeekNode>("aimshot", "leash_distance", SeekNode::DistanceResolveType::Dynamic)
                                    .End()
                                .End()
                            .Sequence(CompositeDecorator::Success) // Determine if a shot should be fired by using weapon trajectory and bounding boxes.
                                .InvertChild<TileQueryNode>(kTileSafeId)
                                .Selector()
                                    .Child<BlackboardSetQueryNode>("aim_override")
                                    .Sequence()
                                        .Child<ShotVelocityQueryNode>(WeaponType::Bullet, "bullet_fire_velocity")
                                        .Child<RayNode>("self_position", "bullet_fire_velocity", "bullet_fire_ray")
                                        .Child<svs::DynamicPlayerBoundingBoxQueryNode>("nearest_target", "target_bounds", 3.0f)
                                        .Child<MoveRectangleNode>("target_bounds", "aimshot", "target_bounds")
                                        .Child<RenderRectNode>("world_camera", "target_bounds", Vector3f(1.0f, 1.0f, 0.0f))
                                        .Child<RenderRayNode>("world_camera", "bullet_fire_ray", 50.0f, Vector3f(1.0f, 1.0f, 0.0f))
                                        .Child<RayRectangleInterceptNode>("bullet_fire_ray", "target_bounds")
                                        .End()
                                    .End()
                                .Child<InputActionNode>(InputAction::Bullet)
                                .Child<BlackboardEraseNode>("aim_override")
                                .End()
                            .End()
                        .End()
                    .End()
                .End()
            .Sequence() // Follow set waypoints.
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

}  // namespace hyperspace
}  // namespace zero
