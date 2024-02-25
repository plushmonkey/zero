#include "CenterLeviBehavior.h"

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
#include <zero/behavior/nodes/TimerNode.h>
#include <zero/behavior/nodes/WaypointNode.h>

namespace zero {
namespace hyperspace {

std::unique_ptr<behavior::BehaviorNode> CenterLeviBehavior::CreateTree(behavior::ExecuteContext& ctx) {
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
        .Sequence() // If in the center safe, move towards the warp tiles.
            .Child<TileQueryNode>(kTileSafeId)
            .Child<ClosestTileQueryNode>("center_warpers", "closest_warper")
            .InvertChild<DistanceThresholdNode>("closest_warper", 25.0f)
            .Child<SeekNode>("closest_warper")
            .End()
        .Selector() // Choose to fight the player or follow waypoints.
            .Sequence()
                .Sequence()
                    .Child<PlayerPositionQueryNode>("self_position")
                    .Child<NearestTargetNode>("nearest_target")
                    .Child<PlayerPositionQueryNode>("nearest_target", "nearest_target_position")
                    .End()
                .Selector()
                    .Sequence() // If the nearest target is close, then activate stealth and cloak.
                        .InvertChild<DistanceThresholdNode>("nearest_target_position", 50.0f)
                        .Parallel()
                            .Sequence()
                                .InvertChild<PlayerStatusQueryNode>(Status_Stealth)
                                .Child<InputActionNode>(InputAction::Stealth)
                                .End()
                            .Sequence()
                                .InvertChild<PlayerStatusQueryNode>(Status_Cloak)
                                .Child<InputActionNode>(InputAction::Cloak)
                                .End()
                            .End()
                        .End()
                    .Sequence() // Path to the nearest camp point if far away from it.
                        .Child<ClosestTileQueryNode>("levi_camp_points", "closest_levi_camp_point")
                        .Child<DistanceThresholdNode>("closest_levi_camp_point", 25.0f)
                        .Child<GoToNode>("closest_levi_camp_point")
                        .End()
                    .Parallel()
                        .Sequence() // Bomb firing sequence
                            .Child<PlayerEnergyPercentThresholdNode>(0.6f) // Only fire bomb if healthy
                            .Selector()
                                .Sequence() // Attempt to fire at closest enemy.
                                    .Child<DistanceThresholdNode>("nearest_target_position", 15.0f)
                                    .Child<VisibilityQueryNode>("nearest_target_position")
                                    .Child<PlayerBoundingBoxQueryNode>("nearest_target", "target_bounds", 12.0f)
                                    .Child<AimNode>(WeaponType::Bomb, "nearest_target", "aim_position")
                                    .Child<MoveRectangleNode>("target_bounds", "aim_position", "target_bounds")
                                    .End()
                                .Sequence() // Attempt to fire at center if enemy is not visible.
                                    .Child<ClosestTileQueryNode>("levi_aim_points", "closest_levi_aim_point")
                                    .Child<VisibilityQueryNode>("closest_levi_aim_point")
                                    .Child<RectangleNode>("closest_levi_aim_point", Vector2f(12, 12), "target_bounds")
                                    .Child<VectorNode>("closest_levi_aim_point", "aim_position")
                                    .End()
                                .End()
                            .Sequence() // After choosing the aim target, attempt to fire the bomb.
                                .Child<FaceNode>("aim_position")
                                .Child<ShotVelocityQueryNode>(WeaponType::Bomb, "bomb_fire_velocity")
                                .Child<RayNode>("self_position", "bomb_fire_velocity", "bomb_fire_ray")
                                .Child<RayRectangleInterceptNode>("bomb_fire_ray", "target_bounds")
                                .InvertChild<TileQueryNode>(kTileSafeId)
                                .Child<InputActionNode>(InputAction::Bomb)
                                .End()
                            .End()
                        .Sequence() // Disable stealth and cloak if nearest enemy is far away
                            .Child<DistanceThresholdNode>("nearest_target_position", 55.0f)
                            .Parallel()
                                .Sequence()
                                    .Child<PlayerStatusQueryNode>(Status_Stealth)
                                    .Child<InputActionNode>(InputAction::Stealth)
                                    .End()
                                .Sequence()
                                    .Child<PlayerStatusQueryNode>(Status_Cloak)
                                    .Child<InputActionNode>(InputAction::Cloak)
                                    .End()
                                .End()
                            .End()
                        .Child<SeekZeroNode>()
                        .End()
                    .Sequence() // Path to the nearest camp point if nothing else to do.
                        .Child<ClosestTileQueryNode>("levi_camp_points", "closest_levi_camp_point")
                        .Selector()
                            .Sequence() // Attempt to seek directly to camp point if visible
                                .Child<VisibilityQueryNode>("closest_levi_camp_point")
                                .Child<SeekNode>("closest_levi_camp_point")
                                .SuccessChild<FaceNode>("aim_position")
                                .End()
                            .Sequence() // Fall back to pathing to camp node
                                .Child<GoToNode>("closest_levi_camp_point")
                                .End()
                            .End()
                        .End()
                    .End()
                .End()
            .Sequence()
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
