#include <string.h>
#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/zones/ZoneController.h>
#include <zero/zones/svs/PowerballBehavior.h>
#include <zero/zones/svs/NexusBehavior.h>
#include <zero/zones/svs/TerrierBehavior.h>
#include <zero/zones/svs/WarbirdBehavior.h>
#include <zero/zones/svs/WarzoneBehavior.h>

namespace zero {
namespace svs {

struct SvsController : ZoneController {
  bool IsZone(Zone zone) override { return zone == Zone::Subgame; }

  void CreateBehaviors(const char* arena_name) override;
};

static SvsController controller;

void SvsController::CreateBehaviors(const char* arena_name) {
  Log(LogLevel::Info, "Registering SVS behaviors.");

  bot->bot_controller->energy_tracker.estimate_type = EnergyHeuristicType::Average;

  auto& repo = bot->bot_controller->behaviors;

  if (strcmp(arena_name, "pb") == 0) {
    repo.Add("powerball", std::make_unique<PowerballBehavior>());

    SetBehavior("powerball");
    return;
  }

  repo.Add("nexus", std::make_unique<NexusBehavior>());
  repo.Add("warbird", std::make_unique<WarbirdBehavior>());
  repo.Add("terrier", std::make_unique<TerrierBehavior>());
  repo.Add("warzone", std::make_unique<WarzoneBehavior>());

  SetBehavior("warbird");

  if (strcmp(arena_name, "warzone") == 0) {
    bot->bot_controller->enable_dynamic_path = false;
    bot->bot_controller->door_solid_method = path::DoorSolidMethod::AlwaysOpen;
  }
}

}  // namespace svs
}  // namespace zero
