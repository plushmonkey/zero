#include "JuggBehavior.h"

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

namespace zero {
namespace mg {

std::unique_ptr<behavior::BehaviorNode> JuggBehavior::CreateTree(behavior::ExecuteContext& ctx) {
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
                    .End()
                .Selector()
                    .Sequence() // Path to target if they aren't immediately visible.
                        .InvertChild<VisibilityQueryNode>("nearest_target_position")
                        .Child<GoToNode>("nearest_target_position")
                        .End()
                    .Sequence() // Aim at target and shoot while seeking them.
                        .Child<AimNode>("nearest_target", "aimshot")
                        .Parallel()
                            .Selector() // Select between hovering around a territory position and seeking to enemy.
                                .Sequence()
                                    .Child<FindTerritoryPosition>("nearest_target", "leash_distance", "territory_position")
                                    .Sequence(CompositeDecorator::Success)
                                        .Child<PositionThreatQueryNode>("self_position", "self_threat", 15.0f, 3.0f)
                                        .Child<PositionThreatQueryNode>("territory_position", "territory_threat", 15.0f, 3.0f)
                                        .Child<ScalarThresholdNode<float>>("territory_threat", 0.2f)
                                        .Child<FindTerritoryPosition>("nearest_target", "leash_distance", "territory_position", true)
                                        .End()
                                    .Sequence(CompositeDecorator::Success)
                                        .InvertChild<ScalarThresholdNode<float>>("self_threat", 0.1f)
                                        .Child<FaceNode>("aimshot")
                                        .End()
                                    .Child<ArriveNode>("territory_position", 15.0f)
                                    .End()
                                .Sequence()
                                    .Child<FaceNode>("aimshot")
                                    .Child<SeekNode>("aimshot", "leash_distance")
                                    .End()
                                .End()
                            .Sequence(CompositeDecorator::Success)
                                .InvertChild<TileQueryNode>(kTileSafeId)
                                .Selector()
                                        .Sequence()
                                            .Child<DistanceNode>("self_position", "nearest_target_position", "dist_sq", true)
                                            .InvertChild<ScalarThresholdNode<float>>("dist_sq", 20.0f * 20.0f)
                                            .Child<ShotVelocityQueryNode>(WeaponType::Bullet, "bullet_fire_velocity")
                                            .Child<RayNode>("self_position", "bullet_fire_velocity", "bullet_fire_ray")
                                            .Child<PlayerBoundingBoxQueryNode>("nearest_target", "target_bounds", 4.0f)
                                            .Child<RayRectangleInterceptNode>("bullet_fire_ray", "target_bounds")
                                            .Child<InputActionNode>(InputAction::Bullet)
                                            .End()
                                        .Sequence()
                                            .Child<ShotVelocityQueryNode>(WeaponType::Bomb, "bomb_fire_velocity")
                                            .Child<RayNode>("self_position", "bomb_fire_velocity", "bomb_fire_ray")
                                            .Child<PlayerBoundingBoxQueryNode>("nearest_target", "target_bounds", 3.0f)
                                            .Child<RayRectangleInterceptNode>("bomb_fire_ray", "target_bounds")
                                            .Child<InputActionNode>(InputAction::Bomb)
                                            .End()
                                    .End()
                                
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

}  // namespace mg
}  // namespace zero
