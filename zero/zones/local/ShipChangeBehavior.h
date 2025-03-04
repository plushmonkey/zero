#pragma once

#include <zero/behavior/Behavior.h>
#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/nodes/MathNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/behavior/nodes/TimerNode.h>

namespace zero {
namespace local {

// Simple behavior that changes ship every couple seconds.
struct ShipChangeBehavior : public behavior::Behavior {
  void OnInitialize(behavior::ExecuteContext& ctx) override {
    // Setup blackboard here for this specific behavior
    ctx.blackboard.Set("request_ship", 0);
  }

  std::unique_ptr<behavior::BehaviorNode> CreateTree(behavior::ExecuteContext& ctx) override {
    using namespace behavior;

    BehaviorBuilder builder;

    u32 kCooldownTicksMin = 100;
    u32 kCooldownTicksMax = 300;

    // clang-format off
    builder
      .Selector()
          .Sequence() // Enter the specified ship if not already in it.
              .Child<TimerExpiredNode>("ship_change_timer")
              .InvertChild<ShipQueryNode>("request_ship")
              .Child<ShipRequestNode>("request_ship")
              .Child<RandomIntNode<u32>>(kCooldownTicksMin, kCooldownTicksMax, "ship_change_cooldown")
              .Child<TimerSetNode>("ship_change_timer", "ship_change_cooldown")
              .Child<ExecuteNode>([](ExecuteContext& ctx) {
                s32 request_ship = ctx.blackboard.ValueOr<int>("request_ship", 0);

                request_ship = (request_ship + 1) % 8;

                ctx.blackboard.Set<s32>("request_ship", request_ship);

                return ExecuteResult::Success;
              })
              .End()
          .End();
    // clang-format on

    return builder.Build();
  }
};

}  // namespace local
}  // namespace zero
