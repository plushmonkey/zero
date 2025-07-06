#include "ExtremeGames.h"

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/zones/ZoneController.h>
#include <zero/zones/extremegames/BaseBehavior.h>
#include <zero/zones/extremegames/CenterBehavior.h>

namespace zero {
namespace eg {

struct ExtremeGamesController : ZoneController {
  bool IsZone(Zone zone) override {
    bot->execute_ctx.blackboard.Erase("eg");
    eg = nullptr;
    return zone == Zone::ExtremeGames;
  }

  void CreateBehaviors(const char* arena_name) override;
  void CreateBases();

  std::unique_ptr<ExtremeGames> eg;
};

static ExtremeGamesController controller;

void ExtremeGamesController::CreateBehaviors(const char* arena_name) {
  Log(LogLevel::Info, "Registering eg behaviors.");

  CreateBases();

  bot->execute_ctx.blackboard.Set("eg", eg.get());

  auto& repo = bot->bot_controller->behaviors;

  repo.Add("center", std::make_unique<CenterBehavior>());
  repo.Add("base", std::make_unique<BaseBehavior>());

  SetBehavior("center");

  bot->bot_controller->energy_tracker.estimate_type = EnergyHeuristicType::Average;
}

void ExtremeGamesController::CreateBases() {
  if (!eg) {
    eg = std::make_unique<ExtremeGames>();
  }

  MapCoord spawn((u16)bot->game->connection.settings.SpawnSettings[0].X,
                 (u16)bot->game->connection.settings.SpawnSettings[0].Y);

  MapBuildConfig cfg = {};

  eg->bases = FindBases(*bot->bot_controller->pathfinder, spawn, cfg);
}

}  // namespace eg
}  // namespace zero
