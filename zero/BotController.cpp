#include "BotController.h"

#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/AimNode.h>
#include <zero/behavior/nodes/InputActionNode.h>
#include <zero/behavior/nodes/MapNode.h>
#include <zero/behavior/nodes/MoveNode.h>
#include <zero/behavior/nodes/PlayerNode.h>
#include <zero/behavior/nodes/RegionNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/behavior/nodes/TargetNode.h>
#include <zero/behavior/nodes/WaypointNode.h>

namespace zero {

BotController::BotController() {
  using namespace std;
  using namespace behavior;

  constexpr u8 kRequestedShip = 0;
  const Vector2f center(512, 512);

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Selector()
        .Sequence()
            .InvertChild<ShipQueryNode>(kRequestedShip)
            .Child<ShipRequestNode>(kRequestedShip)
            .End()
        .Sequence()
            .InvertChild<InRegionNode>(center)
            .Child<WarpNode>()
            .End()
        .Selector()
            .Sequence()
                .Sequence()
                    .Child<NearestTargetNode>("nearest_target")
                    .Child<GetPlayerPositionNode>("nearest_target", "nearest_target_position")
                    .End()
                .Selector()
                    .Sequence()
                        .InvertChild<PositionVisibleNode>("nearest_target_position")
                        .Child<GoToNode>("nearest_target_position")
                        .End()
                    .Sequence()
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
            .Sequence()
                .Child<WaypointNode>("waypoints", "waypoint_index", "waypoint_position", 15.0f)
                .Selector()
                    .Sequence()
                        .InvertChild<PositionVisibleNode>("waypoint_position")
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

  this->behavior_tree = builder.Build();
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
  }

  steering.Reset();

  if (behavior_tree) {
    behavior_tree->Execute(execute_ctx);
  }

  actuator.Update(game, input, steering.force, steering.rotation);
}

}  // namespace zero
