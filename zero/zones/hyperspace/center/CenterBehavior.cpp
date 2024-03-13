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
                    .End()
                .Selector()
                    .Sequence() // Path to target if they aren't immediately visible.
                        .InvertChild<VisibilityQueryNode>("nearest_target_position")
                        .Child<GoToNode>("nearest_target_position")
                        .End()
                    .Sequence() // Aim at target and shoot while seeking them.
                        .Child<AimNode>(WeaponType::Bullet, "nearest_target", "aimshot")
                        .Parallel()
                            .Selector() // Select between hovering around a territory position and seeking to enemy.
                                .Sequence()
                                    .Child<FindTerritoryPosition>("nearest_target", "leash_distance", "territory_position")
                                    .Sequence(CompositeDecorator::Success)
                                        .Child<PositionThreatQueryNode>("self_position", "self_threat", 8.0f, 3.0f)
                                        .Child<RenderTextNode>("ui_camera", Vector2f(512, 600), [](ExecuteContext& ctx) {
                                          std::string str = std::string("Self threat: ") + std::to_string(ctx.blackboard.ValueOr<float>("self_threat", 0.0f));

                                          return RenderTextNode::Request(str, TextColor::White, Layer::TopMost, TextAlignment::Center);
                                        })
                                        .Child<PositionThreatQueryNode>("territory_position", "territory_threat", 8.0f, 3.0f)
                                        .Child<RenderTextNode>("world_camera", "territory_position", [](ExecuteContext& ctx) {
                                          std::string str = std::string("Threat: ") + std::to_string(ctx.blackboard.ValueOr<float>("territory_threat", 0.0f));

                                          return RenderTextNode::Request(str, TextColor::White, Layer::TopMost, TextAlignment::Center);
                                        })
                                        .Child<ScalarThresholdNode<float>>("territory_threat", 0.2f)
                                        .Child<FindTerritoryPosition>("nearest_target", "leash_distance", "territory_position", true)
                                        .End()
                                    .Sequence(CompositeDecorator::Success)
                                        .InvertChild<ScalarThresholdNode<float>>("self_threat", 0.2f)
                                        .Child<FaceNode>("aimshot")
                                        .End()
                                    .Child<ArriveNode>("territory_position", 25.0f)
                                    .Child<RectangleNode>("territory_position", Vector2f(2.0f, 2.0f), "territory_rect")
                                    .Child<RenderRectNode>("world_camera", "territory_rect", Vector3f(0.0f, 1.0f, 0.0f))
                                    .End()
                                .Sequence()
                                    .Child<FaceNode>("aimshot")
                                    .Child<SeekNode>("aimshot", "leash_distance")
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
