#include "TestBehavior.h"

#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/BlackboardNode.h>
#include <zero/behavior/nodes/MapNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/zones/hyperspace/nodes/GlobalGoToNode.h>

namespace zero {
namespace hyperspace {

struct SetRandomPositionNode : public behavior::BehaviorNode {
  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    static const Vector2f kPositions[] = {
        Vector2f(818, 220), Vector2f(840, 530), Vector2f(740, 825), Vector2f(840, 835), Vector2f(290, 815),
        Vector2f(155, 585), Vector2f(210, 205), Vector2f(370, 229), Vector2f(505, 615), Vector2f(50, 50),
    };

    srand((unsigned int)time(nullptr));
    int index = rand() % ZERO_ARRAY_SIZE(kPositions);

    ctx.blackboard.Set("random_position", kPositions[index]);

    return behavior::ExecuteResult::Success;
  }
};

std::unique_ptr<behavior::BehaviorNode> TestBehavior::CreateTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Selector()
        .Sequence() // Enter the specified ship if not already in it.
            .InvertChild<ShipQueryNode>("request_ship")
            .Child<ShipRequestNode>("request_ship")
            .End()
        .Sequence()
            .Sequence(CompositeDecorator::Success)
                .InvertChild<BlackboardSetQueryNode>("random_position")
                .Child<SetRandomPositionNode>()
                .End()
            .Sequence(CompositeDecorator::Success) // Generate a new position when we are near current one.
                .InvertChild<DistanceThresholdNode>("random_position", 10.0f)
                .Child<SetRandomPositionNode>()
                .End()
            .Sequence()
                .Child<GlobalGoToNode>("random_position")
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace hyperspace
}  // namespace zero
