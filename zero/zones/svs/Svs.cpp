#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/zones/ZoneController.h>
#include <zero/zones/svs/WarbirdBehavior.h>
#include <zero/zones/svs/TerrierBehavior.h>

namespace zero {
namespace svs {

struct SvsController : ZoneController {
  bool IsZone(Zone zone) override { return zone == Zone::Subgame; }

  void CreateBehaviors(const char* arena_name) override;
};

static SvsController controller;

void SvsController::CreateBehaviors(const char* arena_name) {
  Log(LogLevel::Info, "Registering SVS behaviors.");

  auto& repo = bot->bot_controller->behaviors;

  repo.Add("warbird", std::make_unique<WarbirdBehavior>());
  repo.Add("terrier", std::make_unique<TerrierBehavior>());

  SetBehavior("warbird");

  bot->bot_controller->energy_tracker.estimate_type = EnergyHeuristicType::Average;
}

}  // namespace svs
}  // namespace zero
