#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/zones/ZoneController.h>
#include <zero/zones/hyperspace/BallBehavior.h>
#include <zero/zones/hyperspace/CenterBehavior.h>
#include <zero/zones/hyperspace/CenterJavBehavior.h>
#include <zero/zones/hyperspace/CenterLeviBehavior.h>
#include <zero/zones/hyperspace/CommandBehavior.h>

#include <format>

namespace zero {
namespace hyperspace {

class FlagCommand : public CommandExecutor {
 public:
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    Player* player = bot.game->player_manager.GetPlayerByName(sender.c_str());
    if (!player) return;

    std::string output = "Not yet implemented.";

    Event::Dispatch(ChatQueueEvent::Private(player->name, output.data()));
  }

  CommandAccessFlags GetAccess() { return CommandAccess_Public | CommandAccess_Private; }
  void SetAccess(CommandAccessFlags flags) {}
  std::vector<std::string> GetAliases() { return {"flag"}; }
  std::string GetDescription() { return "Enables flagging."; }
  int GetSecurityLevel() { return 0; }
};

class ShipItemsCommand : public CommandExecutor {
 public:
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override;

  void SendUsage(const std::string& sender) {
    Event::Dispatch(ChatQueueEvent::Private(sender.data(), "!shipitems [1-8]"));
  }

  CommandAccessFlags GetAccess() { return CommandAccess_Public | CommandAccess_Private; }
  void SetAccess(CommandAccessFlags flags) {}
  std::vector<std::string> GetAliases() { return {"shipitems"}; }
  std::string GetDescription() { return "Prints a list of items the bot currently owns."; }
  int GetSecurityLevel() { return 0; }
};

struct HyperspaceController : ZoneController {
  bool IsZone(Zone zone) override { return zone == Zone::Hyperspace || zone == Zone::Local; }

  void CreateBehaviors(const char* arena_name) override {
    // Create behaviors depending on arena name
    if (isdigit(arena_name[0])) {
      Log(LogLevel::Info, "Registering hyperspace behaviors for public arena.");

      auto& repo = bot->bot_controller->behaviors;

      repo.Add("ball", std::make_unique<BallBehavior>());
      repo.Add("center-levi", std::make_unique<CenterLeviBehavior>());
      repo.Add("center-jav", std::make_unique<CenterJavBehavior>());
      repo.Add("center", std::make_unique<CenterBehavior>());

      command_behavior = std::make_unique<CommandBehavior>();

      SetBehavior("center-jav");

      bot->commands->RegisterCommand(std::make_shared<FlagCommand>());
      bot->commands->RegisterCommand(std::make_shared<ShipItemsCommand>());
    } else {
      Log(LogLevel::Info, "No hyperspace behaviors defined for arena '%s'.", arena_name);
    }
  }

  std::unique_ptr<CommandBehavior> command_behavior;
};

static HyperspaceController controller;

void ShipItemsCommand::Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) {
  Player* player = bot.game->player_manager.GetPlayerByName(sender.c_str());
  if (!player) return;

  auto opt_state = bot.execute_ctx.blackboard.Value<CommandExecuteState>(CommandExecuteState::Key());
  if (opt_state.has_value()) {
    auto& state = opt_state.value();

    if (state.IsPending()) {
      std::string response = std::format("Failed. Awaiting response: {} for {}.", to_string(state.type), state.sender);

      Event::Dispatch(ChatQueueEvent::Private(sender.data(), response.data()));
      return;
    }
  }

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

  std::string command = std::format("?shipitems {}", ship);
  Event::Dispatch(ChatQueueEvent::Public(command.data()));
}

}  // namespace hyperspace
}  // namespace zero
