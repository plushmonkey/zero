#include "BotController.h"

#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/AimNode.h>
#include <zero/behavior/nodes/InputActionNode.h>
#include <zero/behavior/nodes/MapNode.h>
#include <zero/behavior/nodes/MoveNode.h>
#include <zero/behavior/nodes/PlayerNode.h>
#include <zero/behavior/nodes/RegionNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/behavior/nodes/TargetNode.h>

namespace zero {

BotController::BotController() {
  using namespace std;
  using namespace behavior;

  constexpr u8 kRequestedShip = 0;
  const Vector2f center(512, 512);

  auto root = make_unique<SelectorNode>();

  auto ship_join_sequence = make_unique<SequenceNode>();
  ship_join_sequence->Child(make_unique<InvertNode>(make_unique<ShipQueryNode>(kRequestedShip)))
      .Child(make_unique<ShipRequestNode>(kRequestedShip));

  auto center_sequence = make_unique<SequenceNode>();
  center_sequence->Child(make_unique<InvertNode>(make_unique<InRegionNode>(center)))
      .Child(make_unique<InputActionNode>(InputAction::Warp));

  auto select_target_sequence = make_unique<SequenceNode>();
  select_target_sequence->Child(make_unique<NearestTargetNode>("nearest_target"))
      .Child(make_unique<GetPlayerPositionNode>("nearest_target", "nearest_target_position"));

  auto shoot_node = make_unique<SequenceNode>();
  shoot_node->Child(make_unique<InvertNode>(make_unique<TileQueryNode>(kTileSafeId)))
      .Child(make_unique<InputActionNode>(InputAction::Bullet));

  auto chase_and_shoot_node = make_unique<ParallelNode>();
  chase_and_shoot_node->Child(make_unique<FaceNode>("aimshot"))
      .Child(make_unique<SeekNode>("aimshot", "leash_distance"))
      .Child(move(shoot_node));

  auto visible_combat_sequence = make_unique<SequenceNode>();
  visible_combat_sequence->Child(make_unique<AimNode>("nearest_target", "aimshot")).Child(move(chase_and_shoot_node));

  auto path_sequence = make_unique<SequenceNode>();
  path_sequence->Child(make_unique<InvertNode>(make_unique<PositionVisibleNode>("nearest_target_position")))
      .Child(make_unique<GoToNode>("nearest_target_position"));

  auto path_or_fight_selector = make_unique<SelectorNode>();
  path_or_fight_selector->Child(move(path_sequence)).Child(move(visible_combat_sequence));

  auto combat_sequence = make_unique<SequenceNode>();
  combat_sequence->Child(move(select_target_sequence)).Child(move(path_or_fight_selector));

  root->Child(move(ship_join_sequence)).Child(move(center_sequence)).Child(move(combat_sequence));

  this->behavior_tree = move(root);
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
  }

  steering.Reset();

  execute_ctx.blackboard.Set("leash_distance", 15.0f);

  if (behavior_tree) {
    behavior_tree->Execute(execute_ctx);
  }

  actuator.Update(game, input, steering.force, steering.rotation);
}

}  // namespace zero
