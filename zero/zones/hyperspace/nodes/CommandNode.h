#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Clock.h>
#include <zero/game/Logger.h>

namespace zero {
namespace hyperspace {

enum class CommandType { None, ShipItems, Count };
inline const char* to_string(CommandType type) {
  const char* kValues[] = {"None", "ShipItems"};
  static_assert(ZERO_ARRAY_SIZE(kValues) == (size_t)CommandType::Count);
  return kValues[(size_t)type];
}

struct CommandExecuteState {
  CommandType type;
  Tick timeout_tick;
  std::string sender;

  CommandExecuteState(CommandType type, Tick timeout_tick, const std::string& sender)
      : type(type), timeout_tick(timeout_tick), sender(sender) {}

  bool IsPending() const {
    Tick current_tick = GetCurrentTick();

    return !TICK_GT(current_tick, timeout_tick);
  }

  static const char* Key() { return "execute_command_state"; }
  static const char* TreeKey() { return "execute_previous_tree"; }
};

// Returns Success if this is the type of command currently being executed.
struct CommandTypeQuery : public behavior::BehaviorNode {
  CommandTypeQuery(CommandType compare_type) : type(compare_type) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto opt_state = ctx.blackboard.Value<CommandExecuteState>(CommandExecuteState::Key());
    if (!opt_state.has_value()) return behavior::ExecuteResult::Failure;

    CommandExecuteState& command_state = opt_state.value();

    if (command_state.type != type) return behavior::ExecuteResult::Failure;

    return command_state.IsPending() ? behavior::ExecuteResult::Success : behavior::ExecuteResult::Failure;
  }

  CommandType type = CommandType::None;
};

struct ShipItemsParseNode : public behavior::BehaviorNode {
  struct State {
    enum class Section { None, Header, Contents };

    Section section = Section::None;
    std::vector<std::string> lines;

    static const char* Key() { return "shipitems_parse_state"; }
  };

  ShipItemsParseNode() {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto opt_execute_state = ctx.blackboard.Value<CommandExecuteState>(CommandExecuteState::Key());
    if (!opt_execute_state.has_value()) return behavior::ExecuteResult::Failure;

    CommandExecuteState& command_state = opt_execute_state.value();
    if (command_state.type != CommandType::ShipItems) return behavior::ExecuteResult::Failure;

    if (!command_state.IsPending()) {
      Event::Dispatch(ChatQueueEvent::Private(command_state.sender.data(), "Failed to execute ShipItems: Timeout."));

      ctx.blackboard.Erase(CommandExecuteState::Key());
      ctx.blackboard.Erase(State::Key());

      return behavior::ExecuteResult::Failure;
    }

    auto opt_parse_state = ctx.blackboard.Value<State>(State::Key());
    if (!opt_parse_state.has_value()) return behavior::ExecuteResult::Failure;

    State& parse_state = opt_parse_state.value();

    for (auto& mesg : ctx.bot->bot_controller->chat_queue.recv_queue) {
      if (mesg.type != ChatType::Arena) continue;

      bool is_delimiter = IsSectionDelimiter(mesg);

      switch (parse_state.section) {
        case State::Section::None: {
          if (is_delimiter) {
            parse_state.section = State::Section::Header;
          }
        } break;
        case State::Section::Header: {
          if (is_delimiter) {
            parse_state.section = State::Section::Contents;
          }
        } break;
        case State::Section::Contents: {
          if (is_delimiter) {
            for (auto& line : parse_state.lines) {
              Event::Dispatch(ChatQueueEvent::Private(command_state.sender.data(), line.data()));
            }

            ctx.blackboard.Erase(CommandExecuteState::Key());
            ctx.blackboard.Erase(State::Key());

            return behavior::ExecuteResult::Success;
          }

          parse_state.lines.push_back(mesg.message);
        } break;
      }
    }

    ctx.blackboard.Set(State::Key(), parse_state);

    return behavior::ExecuteResult::Running;
  }

  inline bool IsSectionDelimiter(ChatEntry& entry) const {
    if (entry.type != ChatType::Arena) return false;
    return entry.message[0] == '+' && entry.message[1] == '-';
  }
};

}  // namespace hyperspace
}  // namespace zero
