#pragma once

#include <zero/Math.h>
#include <zero/behavior/Behavior.h>
#include <zero/behavior/BehaviorBuilder.h>

namespace zero {
namespace hyperspace {

struct TestBehavior : public behavior::Behavior {
  void OnInitialize(behavior::ExecuteContext& ctx) override {
    // Setup blackboard here for this specific behavior
    ctx.blackboard.Set("request_ship", 0);
  }

  std::unique_ptr<behavior::BehaviorNode> CreateTree(behavior::ExecuteContext& ctx) override;
};

}  // namespace hyperspace
}  // namespace zero
