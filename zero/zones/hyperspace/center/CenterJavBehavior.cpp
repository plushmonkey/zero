#include "CenterJavBehavior.h"

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
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/behavior/nodes/TargetNode.h>
#include <zero/behavior/nodes/ThreatNode.h>
#include <zero/behavior/nodes/TimerNode.h>
#include <zero/behavior/nodes/WaypointNode.h>
#include <zero/zones/svs/nodes/DynamicPlayerBoundingBoxQueryNode.h>
#include <zero/zones/svs/nodes/IncomingDamageQueryNode.h>

namespace zero {
namespace hyperspace {

std::unique_ptr<behavior::BehaviorNode> CenterJavBehavior::CreateTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  // How fast we need to be moving toward the target in tiles for us to decide to shoot a bomb.
// This stops us from shooting bombs slowly all the time.
  constexpr float kForwardSpeedBombRequirement = 1.5f;
  // Bypass the forward speed requirement if we are within this many tiles of the target.
  // This is to prevent us from not shooting when very near someone and circling around.
  constexpr float kNearbyBombBypass = 8.0f;

  const Vector2f center(512, 512);

  // clang-format off
  builder
    .Selector()
        .Sequence() // Enter the specified ship if not already in it.
            .InvertChild<ShipQueryNode>("request_ship")
            .Child<ShipRequestNode>("request_ship")
            .End()
        .Sequence() // Switch to own frequency when possible.
            .Child<PlayerFrequencyCountQueryNode>("self_freq_count")
            .Child<ScalarThresholdNode<size_t>>("self_freq_count", 2)
            .Child<PlayerEnergyPercentThresholdNode>(1.0f)
            .Child<TimerExpiredNode>("next_freq_change_tick")
            .Child<TimerSetNode>("next_freq_change_tick", 300)
            .Child<RandomIntNode<u16>>(5, 89, "random_freq")
            .Child<PlayerChangeFrequencyNode>("random_freq")
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
                    .Child<BulletDistanceNode>("bullet_distance")
                    .End()
                .Sequence(CompositeDecorator::Success) // Set a delay on portal laying cooldown while in safe so we don't place the location there.
                    .Child<TileQueryNode>(kTileIdSafe)
                    .Child<TimerSetNode>("portal_safe_cooldown", 300)
                    .End()
                .Sequence(CompositeDecorator::Success) // If we have a portal but no location, lay one down.
                    .Child<TimerExpiredNode>("portal_safe_cooldown")
                    .Child<ShipItemCountThresholdNode>(ShipItemType::Portal, 1)
                    .InvertChild<ShipPortalPositionQueryNode>()
                    .Child<InputActionNode>(InputAction::Portal)
                    .End()
                .Sequence(CompositeDecorator::Success) // Enable multifire if ship supports it and it's disabled.
                    .Child<ShipCapabilityQueryNode>(ShipCapability_Multifire)
                    .InvertChild<ShipMultifireQueryNode>()
                    .Child<InputActionNode>(InputAction::Multifire)
                    .End()
                .Selector()
                    .Sequence()
                        .Child<PlayerEnergyQueryNode>("nearest_target", "target_energy")
                        .Child<PlayerEnergyQueryNode>("self_energy")
                        .Selector() // Only dodge when far away or enemy might have more energy than us.
                            .Child<GreaterOrEqualThanNode<float>>("target_energy", "self_energy")
                            .Child<DistanceThresholdNode>("nearest_target_position", "bullet_distance")
                            .End()
                        .Child<DodgeIncomingDamage>(0.5f, 45.0f)
                        .End()
                    .Sequence() // Path to target if they aren't immediately visible.
                        .InvertChild<VisibilityQueryNode>("nearest_target_position")
                        .Child<GoToNode>("nearest_target_position")
                        .End()
                    .Sequence() // Aim at target and shoot while seeking them.
                        .Child<AimNode>(WeaponType::Bomb, "nearest_target", "aimshot")
                        .Parallel()
                            .Sequence(CompositeDecorator::Success)
                                .Child<VectorNode>("aimshot", "face_position")
                                .Selector()
                                    .Sequence()
                                        .Child<PlayerEnergyQueryNode>("nearest_target", "target_energy")
                                        .Child<PlayerEnergyQueryNode>("self_energy")
                                        .Child<GreaterOrEqualThanNode<float>>("self_energy", "target_energy")
                                        .Child<SeekNode>("aimshot", 10.0f, SeekNode::DistanceResolveType::Dynamic)
                                        .End()
                                    .Sequence()
                                        .Child<ShipWeaponCooldownQueryNode>(WeaponType::Bomb)
                                        .Child<SeekNode>("nearest_target_position", "leash_distance", SeekNode::DistanceResolveType::Dynamic)
                                        .End()
                                    .Child<SeekNode>("aimshot", 0.0f, SeekNode::DistanceResolveType::Zero)
                                    .End()
                                .End()
                            .Sequence(CompositeDecorator::Success) // Always check incoming damage so we can use it in repel and portal sequences.
                                .Child<RepelDistanceQueryNode>("repel_distance")
                                .Child<svs::IncomingDamageQueryNode>("repel_distance", "incoming_damage")
                                .Child<PlayerCurrentEnergyQueryNode>("self_energy")
                                .End()
                            .Sequence(CompositeDecorator::Success) // If we are in danger but can't repel, use our portal.
                                .InvertChild<ShipItemCountThresholdNode>(ShipItemType::Repel)
                                .Child<ShipPortalPositionQueryNode>() // Check if we have a portal down.
                                .Child<ScalarThresholdNode<float>>("incoming_damage", "self_energy")
                                .Child<TimerExpiredNode>("defense_timer")
                                .Child<InputActionNode>(InputAction::Warp)
                                .Child<TimerSetNode>("defense_timer", 100)
                                .End()
                            .Sequence(CompositeDecorator::Success) // Use repel when in danger.
                                .Child<ShipWeaponCapabilityQueryNode>(WeaponType::Repel)
                                .Child<TimerExpiredNode>("defense_timer")
                                .Child<ScalarThresholdNode<float>>("incoming_damage", "self_energy")
                                .Child<InputActionNode>(InputAction::Repel)
                                .Child<TimerSetNode>("defense_timer", 100)
                                .End()
                            .Sequence(CompositeDecorator::Success)
                                .InvertChild<TileQueryNode>(kTileIdSafe)
                                .Selector()
                                    .Sequence() // Fire bomb
                                        .InvertChild<ShipWeaponCooldownQueryNode>(WeaponType::Bomb)
                                        .Child<VectorSubtractNode>("aimshot", "self_position", "target_direction", true)
                                        .Selector()
                                            .InvertChild<DistanceThresholdNode>("nearest_target_position", kNearbyBombBypass)
                                            .Sequence()
                                                .Child<PlayerVelocityQueryNode>("self_velocity")
                                                .Child<VectorDotNode>("self_velocity", "target_direction", "forward_velocity")
                                                .Child<ScalarThresholdNode<float>>("forward_velocity", kForwardSpeedBombRequirement)
                                                .End()
                                            .End()
                                        .Child<ShotVelocityQueryNode>(WeaponType::Bomb, "bomb_fire_velocity")
                                        .Child<RayNode>("self_position", "bomb_fire_velocity", "bomb_fire_ray")
                                        .Child<svs::DynamicPlayerBoundingBoxQueryNode>("nearest_target", "target_bounds", 3.0f)
                                        .Child<MoveRectangleNode>("target_bounds", "aimshot", "target_bounds")
                                        .Child<RayRectangleInterceptNode>("bomb_fire_ray", "target_bounds")
                                        .Child<InputActionNode>(InputAction::Bomb)
                                        .End()
                                    .Sequence() // Fire bullet
                                        .InvertChild<InputQueryNode>(InputAction::Bomb)
                                        .Child<DistanceNode>("self_position", "nearest_target_position", "dist_sq", true)
                                        .InvertChild<ScalarThresholdNode<float>>("dist_sq", 20.0f * 20.0f)
                                        .Child<ShotVelocityQueryNode>(WeaponType::Bullet, "bullet_fire_velocity")
                                        .Child<RayNode>("self_position", "bullet_fire_velocity", "bullet_fire_ray")
                                        .Child<svs::DynamicPlayerBoundingBoxQueryNode>("nearest_target", "target_bounds", 3.0f)
                                        .Child<MoveRectangleNode>("target_bounds", "aimshot", "target_bounds")
                                        .Child<RayRectangleInterceptNode>("bullet_fire_ray", "target_bounds")
                                        .Child<InputActionNode>(InputAction::Bullet)
                                        .End()
                                    .End()
                                .End() // End weapon firing
                            .End() // End parallel
                        .Sequence()
                          .Child<BlackboardSetQueryNode>("face_position")
                          .Child<RotationThresholdSetNode>(0.35f)
                          .Child<FaceNode>("face_position")
                          .Child<BlackboardEraseNode>("face_position")
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
