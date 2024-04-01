#include "TestBehavior.h"

#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/BlackboardNode.h>
#include <zero/behavior/nodes/InputActionNode.h>
#include <zero/behavior/nodes/MapNode.h>
#include <zero/behavior/nodes/MoveNode.h>
#include <zero/behavior/nodes/PlayerNode.h>
#include <zero/behavior/nodes/RenderNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/zones/devastation/nodes/GetSpawnNode.h>

namespace zero {
namespace deva {

std::unique_ptr<behavior::BehaviorNode> TestBehavior::CreateTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  srand((unsigned int)time(nullptr));

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Selector()
        .Sequence() // Enter the specified ship if not already in it.
            .InvertChild<ShipQueryNode>("request_ship")
            .Child<ShipRequestNode>("request_ship")
            .End()
        .Sequence() // Check if we are in enemy safe
            .Child<PlayerStatusQueryNode>(Status_Safety)
            .Child<GetSpawnNode>(GetSpawnNode::Type::Enemy, "enemy_spawn")
            .InvertChild<DistanceThresholdNode>("enemy_spawn", 2.0f)
            .Child<InputActionNode>(InputAction::Bullet)
            .End()
        .Sequence() // Travel to enemy spawn
            .Child<GetSpawnNode>(GetSpawnNode::Type::Enemy, "enemy_spawn")
            .Child<GoToNode>("enemy_spawn")
            .Child<RenderPathNode>(Vector3f(0, 0, 0))
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace deva
}  // namespace zero
