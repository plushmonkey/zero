#include "CommandBehavior.h"

#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/BehaviorNode.h>

namespace zero {
namespace hyperspace {

std::unique_ptr<behavior::BehaviorNode> CommandBehavior::CreateTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  const Vector2f center(512, 512);

  // clang-format off
  builder
    .Selector()
        .Sequence()
            .Child<CommandTypeQuery>(CommandType::ShipItems)
            .Child<ShipItemsParseNode>()
            .End()
        // If nothing is being executed, restore the old tree.
        .Child<BehaviorSetFromKeyNode>(CommandExecuteState::TreeKey())
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace hyperspace
}  // namespace zero
