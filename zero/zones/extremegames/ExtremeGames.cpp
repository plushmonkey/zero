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

  MapBuildConfig cfg = {};

  MapCoord spawn((u16)bot->game->connection.settings.SpawnSettings[0].X,
                 (u16)bot->game->connection.settings.SpawnSettings[0].Y);
  u32 radius = bot->game->connection.settings.SpawnSettings[0].Radius;

  // Try to find a good starting area for searching for bases.
  constexpr size_t kMaxSpawnTries = 32;
  for (int i = 0; i < kMaxSpawnTries; ++i) {
    s16 rand_x = (s16)(((u32)rand() % (radius * 2)) - radius);
    s16 rand_y = (s16)(((u32)rand() % (radius * 2)) - radius);

    MapCoord coord(spawn.x + rand_x, spawn.y + rand_y);

    if (bot->game->GetMap().CanFit(Vector2f((float)coord.x, (float)coord.y), 14.0f / 16.0f, 0xFFFF)) {
      cfg.spawn = coord;
      break;
    }
  }

  auto opt_base_count = this->bot->config->GetInt("ExtremeGames", "BaseCount");
  if (opt_base_count) {
    cfg.base_count = *opt_base_count;
  }

  eg->bases = FindBases(*bot->bot_controller->pathfinder, cfg);

  for (MapBase& base : eg->bases) {
    base.path = bot->bot_controller->pathfinder->FindPath(bot->game->GetMap(), base.entrance_position,
                                                          base.flagroom_position, 14.0f / 16.0f, 0xFFFF);
    if (!base.path.points.empty()) {
      // Move the entrance a bit inside so we are definitely in the base.
      size_t new_entrance_index = (size_t)(base.path.points.size() * 0.15f);
      base.entrance_position = base.path.points[new_entrance_index];
    }
  }
}

}  // namespace eg
}  // namespace zero
