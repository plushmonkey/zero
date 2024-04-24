#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/Behavior.h>
#include <zero/zones/devastation/Devastation.h>

namespace zero {
namespace deva {

struct CombatRoleQueryNode : public behavior::BehaviorNode {
  CombatRoleQueryNode(CombatRole role) : role(role) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto opt_role = ctx.blackboard.Value<CombatRole>(GetRoleKey());
    if (!opt_role) return behavior::ExecuteResult::Failure;

    return *opt_role == role ? behavior::ExecuteResult::Success : behavior::ExecuteResult::Failure;
  }

  inline static const char* GetRoleKey() { return "combat_role"; }

  CombatRole role = CombatRole::Rusher;
};

struct SetCombatRoleNode : public behavior::BehaviorNode {
  SetCombatRoleNode(CombatRole role) : role(role) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    ctx.blackboard.Set<CombatRole>(CombatRoleQueryNode::GetRoleKey(), role);
    return behavior::ExecuteResult::Success;
  }

  CombatRole role = CombatRole::Rusher;
};

}  // namespace deva
}  // namespace zero
