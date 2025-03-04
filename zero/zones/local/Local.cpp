#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/zones/ZoneController.h>
#include <zero/zones/local/CommandSpamBehavior.h>
#include <zero/zones/local/ShipChangeBehavior.h>

namespace zero {
namespace local {

struct LocalController : ZoneController {
  bool IsZone(Zone zone) override { return zone == Zone::Local; }

  void CreateBehaviors(const char* arena_name) override;
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
  void SetAccess(CommandAccessFlags flags) override {}
  std::vector<std::string> GetAliases() override { return {"setcommand", "setcmd"}; }
  std::string GetDescription() override { return "Sets the command to spam during 'commandspam' behavior."; }
  int GetSecurityLevel() override { return 0; }
};

void LocalController::CreateBehaviors(const char* arena_name) {
  Log(LogLevel::Info, "Registering Local behaviors.");

  auto& repo = bot->bot_controller->behaviors;

  repo.Add("shipchange", std::make_unique<ShipChangeBehavior>());
  repo.Add("commandspam", std::make_unique<CommandSpamBehavior>());

  SetBehavior("shipchange");

  bot->commands->RegisterCommand(std::make_shared<SetCommandCommand>());

  bot->bot_controller->energy_tracker.estimate_type = EnergyHeuristicType::Average;
}

}  // namespace local
}  // namespace zero
