#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Clock.h>
#include <zero/game/Logger.h>

namespace zero {
namespace hyperspace {

enum class CommandType { None, ShipItems, Buy, Sell, Count };
inline const char* to_string(CommandType type) {
  const char* kValues[] = {"None", "ShipItems", "Buy", "Sell"};
  static_assert(ZERO_ARRAY_SIZE(kValues) == (size_t)CommandType::Count);
  return kValues[(size_t)type];
}

enum class Store { Center, Depot };

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

template <CommandType command_type, size_t store_prefix_size>
struct ItemTransactionNode : public behavior::BehaviorNode {
  static constexpr const char* kMatchVector[] = {"You purchased ",
                                                 "You sold ",
                                                 "You cannot buy item",
                                                 "You cannot sell item",
                                                 "No item ",
                                                 "You do not have enough free",
                                                 "No items can be loaded onto a",
                                                 "You cannot buy or sell items",
                                                 "Too many partial matches!",
                                                 "You may only have",
                                                 "more experience to buy item",
                                                 "You do not have enough money to ",
                                                 "is not for sale",
                                                 "is not allowed on a",
                                                 "You do not have any of item",
                                                 "You do not have that many of item",
                                                 "cannot be sold."};

  enum class ResponseType {
    Failure,
    Success,
    Store,
  };

  struct TransactionResponse {
    ResponseType type;
    std::string message;
  };

  struct State {
    // This is the list of items that should be bought/sold.
    std::vector<std::string> request_items;

    // This is the list of items waiting to get a response from server.
    std::vector<std::string> pending_items;

    // This is the list of responses from the commands.
    std::vector<TransactionResponse> responses;

    static const char* Key() { return "transaction_state"; }
    static const char* StoreKey() { return "transaction_store_key"; }
  };

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto opt_execute_state = ctx.blackboard.Value<CommandExecuteState>(CommandExecuteState::Key());
    if (!opt_execute_state.has_value()) return behavior::ExecuteResult::Failure;

    CommandExecuteState& command_state = opt_execute_state.value();
    if (command_state.type != command_type) return behavior::ExecuteResult::Failure;

    if (!command_state.IsPending()) {
      // TODO: Should report exact outcome.
      std::string result = std::string("Failed to execute ") + to_string(command_type) + ": Timeout.";
      Event::Dispatch(ChatQueueEvent::Private(command_state.sender.data(), result.data()));

      ctx.blackboard.Erase(CommandExecuteState::Key());
      ctx.blackboard.Erase(State::Key());
      ctx.blackboard.Erase(State::StoreKey());

      return behavior::ExecuteResult::Failure;
    }

    auto opt_parse_state = ctx.blackboard.Value<State>(State::Key());
    if (!opt_parse_state.has_value()) return behavior::ExecuteResult::Failure;

    State& parse_state = opt_parse_state.value();

    // If there are still items in the request_items list, send the commands.
    if (!parse_state.request_items.empty()) {
      std::string command = "?";

      for (auto& item : parse_state.request_items) {
        if (command_type == CommandType::Buy) {
          command += "|buy " + item;
        } else {
          command += "|sell " + item;
        }

        parse_state.pending_items.push_back(item);
      }

      Event::Dispatch(ChatQueueEvent::Public(command.data()));
      parse_state.request_items.clear();
    }

    // Check the message queue to see if there were any results.
    for (auto& mesg : ctx.bot->bot_controller->chat_queue.recv_queue) {
      if (mesg.type != ChatType::Arena) continue;

      std::string_view mesg_view(mesg.message);

      for (size_t i = 0; i < ZERO_ARRAY_SIZE(kMatchVector); ++i) {
        if (mesg_view.find(kMatchVector[i]) != std::string::npos) {
          TransactionResponse response;

          if (i <= 1) {
            response.type = ResponseType::Success;

            Event::Dispatch(ChatQueueEvent::Private(command_state.sender.data(), mesg.message));
          } else if (i == 2 || i == 3) {
            response.type = ResponseType::Store;
          } else {
            response.type = ResponseType::Failure;

            Event::Dispatch(ChatQueueEvent::Private(command_state.sender.data(), mesg.message));
          }

          response.message = mesg.message;

          parse_state.responses.push_back(response);
          break;
        }
      }
    }

    // All responses received. Check if there's any other store reponses.
    if (parse_state.responses.size() == parse_state.pending_items.size()) {
      // Enough responses came in to match the pending items, so clear pending in case we need to move to another store.
      parse_state.pending_items.clear();

      for (auto& response : parse_state.responses) {
        if (response.type == ResponseType::Store) {
          size_t kNameOffset = store_prefix_size;

          std::string_view item_view(response.message.data() + kNameOffset);
          size_t end = item_view.find(" here.");

          if (end != std::string::npos) {
            item_view = item_view.substr(0, end);

            parse_state.request_items.emplace_back(item_view.data(), item_view.size());

            // TODO: The store should be parsed here, but not important since only center and depot exist right now.
            ctx.blackboard.Set(State::StoreKey(), Store::Depot);
          }
        }
      }

      parse_state.responses.clear();

      // None of the responses told us to go to another store, so end the entire command.
      if (parse_state.request_items.empty()) {
        ctx.blackboard.Erase(CommandExecuteState::Key());
        ctx.blackboard.Erase(State::Key());
        ctx.blackboard.Erase(State::StoreKey());

        return behavior::ExecuteResult::Success;
      }
    }

    ctx.blackboard.Set(State::Key(), parse_state);

    return behavior::ExecuteResult::Success;
  }
};

using BuyNode = ItemTransactionNode<CommandType::Buy, 20>;
using SellNode = ItemTransactionNode<CommandType::Sell, 21>;

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
