#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/zones/ZoneController.h>
#include <zero/zones/devastation/BaseManager.h>
#include <zero/zones/devastation/FrogRaceBehavior.h>
#include <zero/zones/devastation/base/TestBehavior.h>
#include <zero/zones/devastation/center/CenterBehavior.h>

namespace zero {
namespace deva {

struct WarpToCommand : public CommandExecutor {
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    auto split_pos = arg.find(',');

    if (split_pos == std::string::npos) {
      SendUsage(sender);
      return;
    }

    int first_coord = (int)strtol(arg.data(), nullptr, 10);
    int second_coord = (int)strtol(arg.data() + split_pos + 1, nullptr, 10);

    if (first_coord < 0 || second_coord < 0 || first_coord > 1023 || second_coord > 1023) {
      SendUsage(sender);
      return;
    }

    auto self = bot.game->player_manager.GetSelf();
    if (self) {
      self->position = Vector2f((float)first_coord + 0.5f, (float)second_coord + 0.5f);
      self->togglables |= Status_Flash;

      Event::Dispatch(TeleportEvent(*self));
    }
  }

  void SendUsage(const std::string& target_player) {
    std::string usage = "Requires coordinate in x, y form. Ex: warpto 512, 600";
    Event::Dispatch(ChatQueueEvent::Private(target_player.data(), usage.data()));
  }

  CommandAccessFlags GetAccess() override { return CommandAccess_Private; }
  std::vector<std::string> GetAliases() override { return {"warpto"}; }
  std::string GetDescription() override { return "Warps to a provided coord."; }
};

struct DevastationController : ZoneController {
  std::unique_ptr<BaseManager> base_manager;

  bool IsZone(Zone zone) override {
    bot->execute_ctx.blackboard.Erase("base_manager");
    base_manager = nullptr;
    return zone == Zone::Devastation;
  }

  void CreateBehaviors(const char* arena_name) override;
};

static DevastationController controller;

void DevastationController::CreateBehaviors(const char* arena_name) {
  // Create behaviors depending on arena name
  Log(LogLevel::Info, "Registering Devastation behaviors.");

  if (strcmp(arena_name, "frograce") == 0) {
    auto& repo = bot->bot_controller->behaviors;

    repo.Add("race", std::make_unique<FrogRaceBehavior>());

    SetBehavior("race");

    bot->commands->RegisterCommand(std::make_shared<RaceCommand>());
  } else {
    base_manager = std::make_unique<BaseManager>(*this->bot);

    auto& repo = bot->bot_controller->behaviors;

    repo.Add("center", std::make_unique<CenterBehavior>());
    repo.Add("test", std::make_unique<TestBehavior>());

    SetBehavior("center");

    bot->execute_ctx.blackboard.Set("base_manager", base_manager.get());
  }

  bot->bot_controller->energy_tracker.estimate_type = EnergyHeuristicType::Maximum;
  bot->commands->RegisterCommand(std::make_shared<WarpToCommand>());
  bot->commands->SetCommandSecurityLevel("warpto", 10);
}

}  // namespace deva
}  // namespace zero
