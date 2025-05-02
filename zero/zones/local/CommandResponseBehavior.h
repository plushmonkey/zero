#pragma once

#include <zero/ChatQueue.h>
#include <zero/Utility.h>
#include <zero/behavior/Behavior.h>
#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/nodes/BlackboardNode.h>
#include <zero/behavior/nodes/ChatNode.h>
#include <zero/behavior/nodes/MathNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/behavior/nodes/TimerNode.h>

namespace zero {
namespace local {

// Test behavior for outputting a chat message in response to a command.
// This is a convoluted way of responding because it could just be done immediately in the command,
// but it's useful for testing the interaction.
struct CommandResponseBehavior : public behavior::Behavior {
  void OnInitialize(behavior::ExecuteContext& ctx) override {
    // Setup blackboard here for this specific behavior
  }

  std::unique_ptr<behavior::BehaviorNode> CreateTree(behavior::ExecuteContext& ctx) override {
    using namespace behavior;

    BehaviorBuilder builder;

    // clang-format off
    builder
      .Sequence()
        .Child<BlackboardSetQueryNode>(ResponseKey()) // Check if we have something to respond with
        .Child<ChatMessageNode>(ChatMessageNode::PublicBlackboard(ResponseKey()))
        .Child<ChatMessageNode>(ChatMessageNode::TeamBlackboard(ResponseKey()))
        .Child<ChatMessageNode>(ChatMessageNode::PrivateBlackboard(NameKey(), ResponseKey()))
        .Child<ChatMessageNode>(ChatMessageNode::FrequencyBlackboard(FreqKey(), ResponseKey()))
        .Child<BlackboardEraseNode>(ResponseKey()) // Remove the response from the blackboard
        .End();
    // clang-format on

    return builder.Build();
  }

  static const char* NameKey() { return "response_name_key"; }
  static const char* FreqKey() { return "response_freq_key"; }
  static const char* ResponseKey() { return "response_response_key"; }
};

}  // namespace local
}  // namespace zero
