#pragma once

#include <zero/Math.h>
#include <zero/behavior/Behavior.h>
#include <zero/behavior/BehaviorBuilder.h>
#include <zero/zones/devastation/Devastation.h>

namespace zero {
namespace deva {

struct TestBehavior : public behavior::Behavior {
  void OnInitialize(behavior::ExecuteContext& ctx) override {
    // Setup blackboard here for this specific behavior
    ctx.blackboard.Set("request_ship", 1);
    ctx.blackboard.Set("combat_role", CombatRole::Rusher);
  }

  std::unique_ptr<behavior::BehaviorNode> CreateTree(behavior::ExecuteContext& ctx) override;
};

}  // namespace deva
}  // namespace zero
