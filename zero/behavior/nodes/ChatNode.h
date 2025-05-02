#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

struct ChatMessageNode : public BehaviorNode {
  ChatMessageNode(const ChatMessageNode& other)
      : type(other.type),
        message(other.message),
        target_name(other.target_name),
        frequency(other.frequency),
        message_key(other.message_key),
        freq_key(other.freq_key),
        target_name_key(other.target_name_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    if (message_key) {
      auto opt_message = ctx.blackboard.Value<std::string>(message_key);
      if (!opt_message) return ExecuteResult::Failure;
      message = *opt_message;
    }

    switch (type) {
      case ChatType::PublicMacro:
      case ChatType::Public: {
        ctx.bot->bot_controller->chat_queue.SendPublic(message.data());
      } break;
      case ChatType::Team: {
        ctx.bot->bot_controller->chat_queue.SendTeam(message.data());
      } break;
      case ChatType::OtherTeam: {
        if (freq_key) {
          auto opt_freq = ctx.blackboard.Value<u16>(freq_key);
          if (!opt_freq) return ExecuteResult::Failure;
          frequency = *opt_freq;
        }
        ctx.bot->bot_controller->chat_queue.SendFrequency(frequency, message.data());
      } break;
      case ChatType::Private:
      case ChatType::RemotePrivate: {
        if (target_name_key) {
          auto opt_name = ctx.blackboard.Value<std::string>(target_name_key);
          if (!opt_name) return ExecuteResult::Failure;
          target_name = *opt_name;
        }

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

    node.type = ChatType::Private;
    node.message = message;
    node.target_name = target_name;

    return node;
  }

  static ChatMessageNode PublicBlackboard(const char* message_key) {
    ChatMessageNode node;

    node.type = ChatType::Public;
    node.message_key = message_key;

    return node;
  }

  static ChatMessageNode TeamBlackboard(const char* message_key) {
    ChatMessageNode node;

    node.type = ChatType::Team;
    node.message_key = message_key;

    return node;
  }

  static ChatMessageNode FrequencyBlackboard(const char* freq_key, const char* message_key) {
    ChatMessageNode node;

    node.type = ChatType::OtherTeam;
    node.freq_key = freq_key;
    node.message_key = message_key;

    return node;
  }

  static ChatMessageNode PrivateBlackboard(const char* target_name_key, const char* message_key) {
    ChatMessageNode node;

    node.type = ChatType::Private;
    node.target_name_key = target_name_key;
    node.message_key = message_key;

    return node;
  }

  ChatType type = ChatType::Public;
  std::string message;
  std::string target_name;
  u16 frequency = 0;

  const char* message_key = nullptr;
  const char* freq_key = nullptr;
  const char* target_name_key = nullptr;

 private:
  ChatMessageNode() {}
};

}  // namespace behavior
}  // namespace zero
