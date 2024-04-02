#include "TestBehavior.h"

#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/AttachNode.h>
#include <zero/behavior/nodes/BlackboardNode.h>
#include <zero/behavior/nodes/InputActionNode.h>
#include <zero/behavior/nodes/MapNode.h>
#include <zero/behavior/nodes/MathNode.h>
#include <zero/behavior/nodes/MoveNode.h>
#include <zero/behavior/nodes/PlayerNode.h>
#include <zero/behavior/nodes/RenderNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/behavior/nodes/TimerNode.h>
#include <zero/zones/devastation/nodes/GetAttachTargetNode.h>
#include <zero/zones/devastation/nodes/GetSpawnNode.h>

namespace zero {
namespace deva {

std::unique_ptr<behavior::BehaviorNode> TestBehavior::CreateTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  srand((unsigned int)time(nullptr));

  BehaviorBuilder builder;
  Rectangle center_rect = Rectangle::FromPositionRadius(Vector2f(512, 512), 64.0f);

  // clang-format off
  builder
    .Selector()
        .Sequence() // Enter the specified ship if not already in it.
            .InvertChild<ShipQueryNode>("request_ship")
            .Child<ShipRequestNode>("request_ship")
            .End()
        .Sequence() // If we are attached to someone, detach.
            .Child<AttachedQueryNode>()
            .Child<DetachNode>()
            .End()
        .Sequence() // If we are in center safe, try to find a good attach target
            .Child<PlayerPositionQueryNode>("self_position")
            .Child<RectangleContainsNode>(center_rect, "self_position")
            .Child<TimerExpiredNode>("next_attach_tick")
            .Child<TimerSetNode>("next_attach_tick", 50)
            .Child<GetAttachTargetNode>(center_rect, "attach_target")
            .Child<AttachNode>("attach_target")
            .Child<DebugPrintNode>("attach request")
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
