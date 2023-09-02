#pragma once

#include <zero/Math.h>
#include <zero/behavior/Behavior.h>
#include <zero/behavior/BehaviorBuilder.h>
#include <zero/zones/hyperspace/nodes/CommandNode.h>

namespace zero {
namespace hyperspace {

struct CommandBehavior : public behavior::Behavior {
  void OnInitialize(behavior::ExecuteContext& ctx) override {}

  std::unique_ptr<behavior::BehaviorNode> CreateTree(behavior::ExecuteContext& ctx) override;
};

}  // namespace hyperspace
}  // namespace zero
