#include "BotController.h"

#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/AimNode.h>
#include <zero/behavior/nodes/InputActionNode.h>
#include <zero/behavior/nodes/MapNode.h>
#include <zero/behavior/nodes/MathNode.h>
#include <zero/behavior/nodes/MoveNode.h>
#include <zero/behavior/nodes/PlayerNode.h>
#include <zero/behavior/nodes/RegionNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/behavior/nodes/TargetNode.h>
#include <zero/behavior/nodes/WaypointNode.h>

namespace zero {

std::unique_ptr<behavior::BehaviorNode> BuildHyperspaceWarbirdCenter(int ship) {
  using namespace behavior;

  BehaviorBuilder builder;

  const Vector2f center(512, 512);

  // clang-format off
  builder
    .Selector()
        .Sequence() // Enter the specified ship if not already in it.
            .InvertChild<ShipQueryNode>(ship)
            .Child<ShipRequestNode>(ship)
            .End()
        .Sequence() // Warp back to center.
            .InvertChild<RegionContainQueryNode>(center)
            .Child<WarpNode>()
            .End()
        .Selector() // Choose to fight the player or follow waypoints.
            .Sequence() // Find nearest target and either path to them or seek them directly.
                .Sequence()
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
                            .Child<FaceNode>("aimshot")
                            .Child<SeekNode>("aimshot", "leash_distance")
                            .Sequence()
                                .InvertChild<TileQueryNode>(kTileSafeId)
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

std::unique_ptr<behavior::BehaviorNode> BuildHyperspaceLeviCenter(int ship) {
  using namespace behavior;

  BehaviorBuilder builder;

  const Vector2f center(512, 512);

  // clang-format off
  builder
    .Selector()
        .Sequence() // Enter the specified ship if not already in it.
            .InvertChild<ShipQueryNode>(ship)
            .Child<ShipRequestNode>(ship)
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
                    .Child<NearestTargetNode>("nearest_target")
                    .Child<PlayerPositionQueryNode>("nearest_target", "nearest_target_position")
                    .End()
                .Selector()
                    .Sequence() // If the nearest target is close, then activate stealth and cloak.
                        .InvertChild<DistanceThresholdNode>("nearest_target_position", 40.0f)
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
                    .Sequence() // Aim at the target and shoot without moving toward them.
                        .Child<DistanceThresholdNode>("nearest_target_position", 15.0f)
                      //.Child<AimNode>("nearest_target", "aimshot")
                        .Child<VisibilityQueryNode>("nearest_target_position")
                        .Child<FaceNode>("nearest_target_position")
                        .Child<ShotVelocityQueryNode>(WeaponType::Bomb, "bomb_fire_velocity")
                        .Child<PlayerBoundingBoxQueryNode>("nearest_target", "target_bounds", 10.0f)
                        .Child<PlayerPositionQueryNode>("self_position")
                        .Child<RayNode>("self_position", "bomb_fire_velocity", "bomb_fire_ray")
                        .Child<RayRectInterceptNode>("bomb_fire_ray", "target_bounds")
                        .InvertChild<TileQueryNode>(kTileSafeId)
                        .Child<InputActionNode>(InputAction::Bomb)
                        .End()
                    .Sequence() // Path to the nearest camp point if nothing else to do.
                        .Child<ClosestTileQueryNode>("levi_camp_points", "closest_levi_camp_point")
                        .Child<GoToNode>("closest_levi_camp_point")
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

BotController::BotController() {
  typedef std::unique_ptr<behavior::BehaviorNode> (*ShipBuilder)(int ship);

  constexpr int kShip = 0;

  // clang-format off
  ShipBuilder builders[] = {
    BuildHyperspaceWarbirdCenter,
    BuildHyperspaceWarbirdCenter,
    BuildHyperspaceWarbirdCenter,
    BuildHyperspaceLeviCenter,
    BuildHyperspaceWarbirdCenter,
    BuildHyperspaceWarbirdCenter,
    BuildHyperspaceWarbirdCenter,
    BuildHyperspaceWarbirdCenter,
  };
  // clang-format on

  this->behavior_tree = builders[kShip](kShip);
  this->input = nullptr;
}

void BotController::Update(float dt, Game& game, InputState& input, behavior::ExecuteContext& execute_ctx) {
  this->input = &input;

  // TOOD: Rebuild on ship change and use ship radius.
  if (pathfinder == nullptr) {
    auto processor = std::make_unique<path::NodeProcessor>(game);

    region_registry = std::make_unique<RegionRegistry>();
    region_registry->CreateAll(game.GetMap(), 16.0f / 14.0f);

    pathfinder = std::make_unique<path::Pathfinder>(std::move(processor), *region_registry);

    pathfinder->CreateMapWeights(game.GetMap(), 14.0f / 16.0f);

    execute_ctx.blackboard.Set("leash_distance", 15.0f);

    std::vector<Vector2f> waypoints{
        Vector2f(440, 460),
        Vector2f(565, 465),
        Vector2f(570, 565),
        Vector2f(415, 590),
    };

    execute_ctx.blackboard.Set("waypoints", waypoints);

    std::vector<Vector2f> center_warpers{
        Vector2f(500, 500),
        Vector2f(523, 500),
        Vector2f(523, 523),
        Vector2f(500, 523),
    };

    execute_ctx.blackboard.Set("center_warpers", center_warpers);

    std::vector<Vector2f> levi_camp_points{
        Vector2f(630, 475),
        Vector2f(600, 600),
        Vector2f(420, 605),
        Vector2f(410, 440),
    };

    execute_ctx.blackboard.Set("levi_camp_points", levi_camp_points);
  }

  steering.Reset();

  if (behavior_tree) {
    behavior_tree->Execute(execute_ctx);
  }

  actuator.Update(game, input, steering.force, steering.rotation);
}

}  // namespace zero
