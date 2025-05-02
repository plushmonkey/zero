#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/zones/ZoneController.h>
#include <zero/zones/local/ArenaSpamBehavior.h>
#include <zero/zones/local/CommandResponseBehavior.h>
#include <zero/zones/local/CommandSpamBehavior.h>
#include <zero/zones/local/ShipChangeBehavior.h>

#include <string>

namespace zero {
namespace local {

struct LocalController : ZoneController {
  bool IsZone(Zone zone) override { return zone == Zone::Local; }

  void CreateBehaviors(const char* arena_name) override;

  void HandleEvent(const BehaviorChangeEvent& event) override;
};

static LocalController controller;

class SetCommandCommand : public CommandExecutor {
 public:
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    Player* player = bot.game->player_manager.GetPlayerByName(sender.c_str());
    if (!player) return;

    if (arg.empty()) {
      Event::Dispatch(ChatQueueEvent::Private(player->name, "Usage: !setcommand lag"));
      return;
    }

    std::string command;

    if (arg[0] != '?') {
      command = "?" + arg;
    } else {
      command = arg;
    }

    bot.execute_ctx.blackboard.Set<std::string>(CommandSpamBehavior::CommandKey(), command);

    std::string output = "Command set to: " + command;
    Event::Dispatch(ChatQueueEvent::Private(player->name, output.data()));
  }

  CommandAccessFlags GetAccess() override { return CommandAccess_Private; }
  std::vector<std::string> GetAliases() override { return {"setcommand", "setcmd"}; }
  std::string GetDescription() override { return "Sets the command to spam during 'commandspam' behavior."; }
};

// Test command that uses a behavior tree to output the supplied argument.
class BehaviorEchoCommand : public CommandExecutor {
 public:
  BehaviorEchoCommand(LocalController& controller) : controller(controller) {}

  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    if (arg.empty()) return;

    auto player = bot.bot_controller->game.player_manager.GetPlayerByName(sender.data());
    if (!player) return;

    bot.execute_ctx.blackboard.Set<std::string>(CommandResponseBehavior::NameKey(), sender);
    bot.execute_ctx.blackboard.Set<u16>(CommandResponseBehavior::FreqKey(), player->frequency);
    bot.execute_ctx.blackboard.Set<std::string>(CommandResponseBehavior::ResponseKey(), arg);
  }

  CommandAccessFlags GetAccess() override { return CommandAccess_Private; }

  int GetSecurityLevel() const override {
    auto& bot_controller = *controller.bot->bot_controller;

    // Effectively disable when not in the specified behavior.
    if (bot_controller.behavior_name.compare("response") != 0) {
      return 99999;
    }

    return CommandExecutor::GetSecurityLevel();
  }

  std::vector<std::string> GetAliases() override { return {"echo"}; }
  std::string GetDescription() override { return "Test command for command-behavior interaction."; }

  LocalController& controller;
};

void LocalController::CreateBehaviors(const char* arena_name) {
  Log(LogLevel::Info, "Registering Local behaviors.");

  auto& repo = bot->bot_controller->behaviors;

  repo.Add("shipchange", std::make_unique<ShipChangeBehavior>());
  repo.Add("commandspam", std::make_unique<CommandSpamBehavior>());
  repo.Add("arenaspam", std::make_unique<ArenaSpamBehavior>());
  repo.Add("response", std::make_unique<CommandResponseBehavior>());

  SetBehavior("shipchange");

  bot->commands->RegisterCommand(std::make_shared<BehaviorEchoCommand>(*this));

  bot->bot_controller->energy_tracker.estimate_type = EnergyHeuristicType::Average;

  bot->bot_controller->chat_queue.flood_limit = 3;
}

void LocalController::HandleEvent(const BehaviorChangeEvent& event) {
  if (!in_zone) return;

  std::string message;

  if (!event.previous.empty()) {
    message = "Setting behavior from " + event.previous + " to " + event.name + ".";
  } else {
    message = "Setting behavior to " + event.name + ".";
  }

  bot->bot_controller->chat_queue.SendPublic(message.data());

  // Remove the commands that only exist within certain behavior trees
  bot->commands->UnregisterCommand("setcommand");

  // If we are changing to commandspam, register the necessary commands.
  if (event.name.compare("commandspam") == 0) {
    bot->commands->RegisterCommand(std::make_shared<SetCommandCommand>());
  }
}

}  // namespace local
}  // namespace zero
