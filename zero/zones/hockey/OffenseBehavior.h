#pragma once

#include <zero/behavior/Behavior.h>

namespace zero {
namespace hz {

struct OffenseBehavior : public behavior::Behavior {
  void OnInitialize(behavior::ExecuteContext& ctx) override {
    // Setup blackboard here for this specific behavior
    ctx.blackboard.Set("request_ship", 0);
  }

  std::unique_ptr<behavior::BehaviorNode> CreateTree(behavior::ExecuteContext& ctx) override;
};

}  // namespace hz
}  // namespace zero
