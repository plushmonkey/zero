#include "Hyperspace.h"

#include <zero/BotController.h>
#include <zero/Utility.h>
#include <zero/ZeroBot.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/zones/ZoneController.h>
#include <zero/zones/hyperspace/CommandBehavior.h>
#include <zero/zones/hyperspace/base/TestBehavior.h>
#include <zero/zones/hyperspace/center/BallBehavior.h>
#include <zero/zones/hyperspace/center/CenterBehavior.h>
#include <zero/zones/hyperspace/center/CenterJavBehavior.h>
#include <zero/zones/hyperspace/center/CenterLeviBehavior.h>

namespace zero {
namespace hyperspace {

struct HyperspaceController : ZoneController {
  bool IsZone(Zone zone) override { return zone == Zone::Hyperspace; }

  void CreateBehaviors(const char* arena_name) override;

  std::unique_ptr<CommandBehavior> command_behavior;
};

static HyperspaceController controller;

class FlagCommand : public CommandExecutor {
 public:
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    Player* player = bot.game->player_manager.GetPlayerByName(sender.c_str());
    if (!player) return;

    std::string output = "Not yet implemented.";

    Event::Dispatch(ChatQueueEvent::Private(player->name, output.data()));
  }

  CommandAccessFlags GetAccess() override { return CommandAccess_Private; }
  void SetAccess(CommandAccessFlags flags) override {}
  std::vector<std::string> GetAliases() override { return {"flag"}; }
  std::string GetDescription() override { return "Enables flagging."; }
  int GetSecurityLevel() override { return 0; }
};

class ParseResponseCommand : public CommandExecutor {
 protected:
  bool CanExecute(ZeroBot& bot, const std::string& sender) {
    auto opt_state = bot.execute_ctx.blackboard.Value<CommandExecuteState>(CommandExecuteState::Key());

    if (opt_state.has_value()) {
      auto& state = opt_state.value();

      if (state.IsPending()) {
        std::string response =
            std::string("Failed. Awaiting response: ") + to_string(state.type) + " for " + state.sender + ".";

        Event::Dispatch(ChatQueueEvent::Private(sender.data(), response.data()));
        return false;
      }
    }

    return true;
  }
};

class BuyCommand : public ParseResponseCommand {
 public:
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    Player* player = bot.game->player_manager.GetPlayerByName(sender.c_str());
    if (!player) return;

    if (!CanExecute(bot, sender)) return;
    if (arg.empty()) return SendUsage(sender);

    constexpr Tick kTimeoutTicks = 3000;

    bot.execute_ctx.blackboard.Set(
        CommandExecuteState::Key(),
        CommandExecuteState(CommandType::Buy, MAKE_TICK(GetCurrentTick() + kTimeoutTicks), sender));

    BuyNode::State buy_state;
    std::vector<std::string_view> items = SplitString(arg, "|");

    buy_state.request_items.reserve(items.size());
    for (auto& item : items) {
      buy_state.request_items.emplace_back(item.data(), item.size());
    }

    bot.execute_ctx.blackboard.Set(BuyNode::State::Key(), buy_state);
    bot.execute_ctx.blackboard.Set(BuyNode::State::StoreKey(), Store::Center);

    // Store the old behavior name so it can be reverted on completion.
    bot.execute_ctx.blackboard.Set(CommandExecuteState::TreeKey(), bot.bot_controller->behavior_name);

    controller.command_behavior->OnInitialize(bot.execute_ctx);
    bot.bot_controller->SetBehavior("command-parse", controller.command_behavior->CreateTree(bot.execute_ctx));
  }

  void SendUsage(const std::string& sender) {
    Event::Dispatch(ChatQueueEvent::Private(sender.data(), "Usage: !buy close combat|radiating coils"));
  }

  CommandAccessFlags GetAccess() override { return CommandAccess_Public | CommandAccess_Private; }
  void SetAccess(CommandAccessFlags flags) override {}
  std::vector<std::string> GetAliases() override { return {"buy"}; }
  std::string GetDescription() override { return "Buys items."; }
  int GetSecurityLevel() override { return 0; }
};

class SellCommand : public ParseResponseCommand {
 public:
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    Player* player = bot.game->player_manager.GetPlayerByName(sender.c_str());
    if (!player) return;

    if (!CanExecute(bot, sender)) return;
    if (arg.empty()) return SendUsage(sender);

    constexpr Tick kTimeoutTicks = 3000;

    bot.execute_ctx.blackboard.Set(
        CommandExecuteState::Key(),
        CommandExecuteState(CommandType::Sell, MAKE_TICK(GetCurrentTick() + kTimeoutTicks), sender));

    SellNode::State sell_state;
    std::vector<std::string_view> items = SplitString(arg, "|");

    sell_state.request_items.reserve(items.size());
    for (auto& item : items) {
      sell_state.request_items.emplace_back(item.data(), item.size());
    }

    bot.execute_ctx.blackboard.Set(SellNode::State::Key(), sell_state);
    bot.execute_ctx.blackboard.Set(SellNode::State::StoreKey(), Store::Center);

    // Store the old behavior name so it can be reverted on completion.
    bot.execute_ctx.blackboard.Set(CommandExecuteState::TreeKey(), bot.bot_controller->behavior_name);

    controller.command_behavior->OnInitialize(bot.execute_ctx);
    bot.bot_controller->SetBehavior("command-parse", controller.command_behavior->CreateTree(bot.execute_ctx));
  }

  void SendUsage(const std::string& sender) {
    Event::Dispatch(ChatQueueEvent::Private(sender.data(), "Usage: !sell close combat|radiating coils"));
  }

  CommandAccessFlags GetAccess() override { return CommandAccess_Public | CommandAccess_Private; }
  void SetAccess(CommandAccessFlags flags) override {}
  std::vector<std::string> GetAliases() override { return {"sell"}; }
  std::string GetDescription() override { return "Sells items."; }
  int GetSecurityLevel() override { return 0; }
};

class ShipItemsCommand : public ParseResponseCommand {
 public:
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    Player* player = bot.game->player_manager.GetPlayerByName(sender.c_str());
    if (!player) return;

    if (!CanExecute(bot, sender)) return;
    if (arg.empty() || !isdigit(arg[0])) return SendUsage(sender);

    int ship = (arg[0] - '0');

    if (ship < 1 || ship > 8) return SendUsage(sender);

    constexpr Tick kTimeoutTicks = 800;

    bot.execute_ctx.blackboard.Set(
        CommandExecuteState::Key(),
        CommandExecuteState(CommandType::ShipItems, MAKE_TICK(GetCurrentTick() + kTimeoutTicks), sender));

    bot.execute_ctx.blackboard.Set(ShipItemsParseNode::State::Key(), ShipItemsParseNode::State());

    // Store the old behavior name so it can be reverted on completion.
    bot.execute_ctx.blackboard.Set(CommandExecuteState::TreeKey(), bot.bot_controller->behavior_name);

    controller.command_behavior->OnInitialize(bot.execute_ctx);
    bot.bot_controller->SetBehavior("command-parse", controller.command_behavior->CreateTree(bot.execute_ctx));

    std::string command = std::string("?shipitems ") + std::to_string(ship);
    Event::Dispatch(ChatQueueEvent::Public(command.data()));
  }

  void SendUsage(const std::string& sender) {
    Event::Dispatch(ChatQueueEvent::Private(sender.data(), "!shipitems [1-8]"));
  }

  CommandAccessFlags GetAccess() override { return CommandAccess_Public | CommandAccess_Private; }
  void SetAccess(CommandAccessFlags flags) override {}
  std::vector<std::string> GetAliases() override { return {"shipitems"}; }
  std::string GetDescription() override { return "Prints a list of items the bot currently owns."; }
  int GetSecurityLevel() override { return 0; }
};

void HyperspaceController::CreateBehaviors(const char* arena_name) {
  // Create behaviors depending on arena name
  if (isdigit(arena_name[0])) {
    Log(LogLevel::Info, "Registering hyperspace behaviors for public arena.");

    auto& repo = bot->bot_controller->behaviors;

    repo.Add("ball", std::make_unique<BallBehavior>());
    repo.Add("center-levi", std::make_unique<CenterLeviBehavior>());
    repo.Add("center-jav", std::make_unique<CenterJavBehavior>());
    repo.Add("center", std::make_unique<CenterBehavior>());
    repo.Add("test", std::make_unique<TestBehavior>());

    command_behavior = std::make_unique<CommandBehavior>();

    SetBehavior("center");

    bot->commands->RegisterCommand(std::make_shared<FlagCommand>());
    bot->commands->RegisterCommand(std::make_shared<ShipItemsCommand>());
    bot->commands->RegisterCommand(std::make_shared<BuyCommand>());
    bot->commands->RegisterCommand(std::make_shared<SellCommand>());
  } else {
    Log(LogLevel::Info, "No hyperspace behaviors defined for arena '%s'.", arena_name);
  }
}

}  // namespace hyperspace
}  // namespace zero
