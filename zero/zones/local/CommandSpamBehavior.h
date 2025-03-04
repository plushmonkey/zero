#pragma once

#include <zero/ChatQueue.h>
#include <zero/behavior/Behavior.h>
#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/nodes/MathNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/behavior/nodes/TimerNode.h>

namespace zero {
namespace local {

// Simple behavior that spams a command.
struct CommandSpamBehavior : public behavior::Behavior {
  void OnInitialize(behavior::ExecuteContext& ctx) override {
    // Setup blackboard here for this specific behavior
    ctx.blackboard.Set<std::string>(CommandKey(), "?lag");
  }

  std::unique_ptr<behavior::BehaviorNode> CreateTree(behavior::ExecuteContext& ctx) override {
    using namespace behavior;

    BehaviorBuilder builder;

    // clang-format off
    builder
      .Selector()
          .Child<ExecuteNode>([](ExecuteContext& ctx) {
            auto opt_cmd = ctx.blackboard.Value<std::string>(CommandKey());

            if (!opt_cmd) return ExecuteResult::Failure;
            const std::string& cmd = *opt_cmd;

            auto& chat_queue = ctx.bot->bot_controller->chat_queue;

            // Spam as fast as the queue allows.
            if (chat_queue.IsEmpty()) {
              Event::Dispatch(ChatQueueEvent::Public(cmd.data()));
            }

            return ExecuteResult::Success;
          })
          .End();
    // clang-format on

    return builder.Build();
  }

  static inline const char* CommandKey() { return "command_spam_cmd"; }
};

}  // namespace local
}  // namespace zero
