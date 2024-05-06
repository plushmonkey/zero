#include <zero/BotController.h>
#include <zero/Utility.h>
#include <zero/ZeroBot.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/zones/ZoneController.h>
#include <zero/zones/extremegames/CenterBehavior.h>

namespace zero {
namespace eg {

struct ExtremeGamesController : ZoneController {
  bool IsZone(Zone zone) override { return zone == Zone::ExtremeGames; }

  void CreateBehaviors(const char* arena_name) override;
};

static ExtremeGamesController controller;

void ExtremeGamesController::CreateBehaviors(const char* arena_name) {
  Log(LogLevel::Info, "Registering eg behaviors.");

  auto& repo = bot->bot_controller->behaviors;

  repo.Add("center", std::make_unique<CenterBehavior>());

  SetBehavior("center");

  bot->bot_controller->energy_tracker.estimate_type = EnergyHeuristicType::Initial;
}

}  // namespace eg
}  // namespace zero
