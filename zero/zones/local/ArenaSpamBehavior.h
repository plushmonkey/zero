#pragma once

#include <zero/ChatQueue.h>
#include <zero/Utility.h>
#include <zero/behavior/Behavior.h>
#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/nodes/MathNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/behavior/nodes/TimerNode.h>
#include <random>

namespace zero {
namespace local {

// Simple behavior that spams arena changes.
struct ArenaSpamBehavior : public behavior::Behavior {
  void OnInitialize(behavior::ExecuteContext& ctx) override {
    // Setup blackboard here for this specific behavior
    ctx.blackboard.Set<u32>("arena_move_timer", MAKE_TICK(GetCurrentTick() + 100));
  }

  std::unique_ptr<behavior::BehaviorNode> CreateTree(behavior::ExecuteContext& ctx) override {
    using namespace behavior;

    BehaviorBuilder builder;

    // clang-format off
    builder
      .Sequence()
        .Child<TimerExpiredNode>("arena_move_timer")
        .Child<ExecuteNode>([](ExecuteContext& ctx) {
          char random_name[13] = {};

          std::random_device rd;
          std::uniform_int_distribution<int> dist(0, 26);

          for (size_t i = 0; i < sizeof(random_name) - 1; ++i) {
            random_name[i] = 'a' + dist(rd);
          }

          auto login_pair = ParseLoginArena(random_name);

          ctx.bot->game->connection.SendArenaLogin(8, 0, 1920, 1080, login_pair.first, login_pair.second.data());

          return ExecuteResult::Success;
        })
      .End();
    // clang-format on

    return builder.Build();
  }
};

}  // namespace local
}  // namespace zero
