#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

struct ChatMessageNode : public BehaviorNode {
  ChatMessageNode(const ChatMessageNode& other)
      : type(other.type), message(other.message), target_name(other.target_name), frequency(other.frequency) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    switch (type) {
      case ChatType::PublicMacro:
      case ChatType::Public: {
        ctx.bot->bot_controller->chat_queue.SendPublic(message.data());
      } break;
      case ChatType::Team: {
        ctx.bot->bot_controller->chat_queue.SendTeam(message.data());
      } break;
      case ChatType::OtherTeam: {
        ctx.bot->bot_controller->chat_queue.SendFrequency(frequency, message.data());
      } break;
      case ChatType::Private:
      case ChatType::RemotePrivate: {
        ctx.bot->bot_controller->chat_queue.SendPrivate(target_name.data(), message.data());
      } break;
      default: {
        return ExecuteResult::Failure;
      } break;
    }
    return ExecuteResult::Success;
  }

  static ChatMessageNode Public(const char* message) {
    ChatMessageNode node;

    node.type = ChatType::Public;
    node.message = message;

    return node;
  }

  static ChatMessageNode Team(const char* message) {
    ChatMessageNode node;

    node.type = ChatType::Team;
    node.message = message;

    return node;
  }

  static ChatMessageNode Frequency(u16 frequency, const char* message) {
    ChatMessageNode node;

    node.type = ChatType::OtherTeam;
    node.message = message;
    node.frequency = frequency;

    return node;
  }

  static ChatMessageNode Private(const char* target_name, const char* message) {
    ChatMessageNode node;

    node.type = ChatType::Public;
    node.message = message;

    return node;
  }

  ChatType type = ChatType::Public;
  std::string message;
  std::string target_name;
  u16 frequency = 0;

 private:
  ChatMessageNode() {}
};

}  // namespace behavior
}  // namespace zero
