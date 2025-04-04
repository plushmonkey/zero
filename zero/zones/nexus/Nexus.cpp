#include <string.h>
#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/zones/ZoneController.h>
#include <zero/zones/nexus/NexusABehavior.h>
#include <zero/zones/nexus/NexusBehavior.h>

namespace zero {
namespace nexus {

struct NexusController : ZoneController {
  bool IsZone(Zone zone) override { return zone == Zone::Nexus; }

  void CreateBehaviors(const char* arena_name) override;
};

static NexusController controller;

void NexusController::CreateBehaviors(const char* arena_name) {
  Log(LogLevel::Info, "Registering Nexus behaviors.");

  bot->bot_controller->energy_tracker.estimate_type = EnergyHeuristicType::Average;

  auto& repo = bot->bot_controller->behaviors;

  repo.Add("nexus", std::make_unique<NexusBehavior>());
  repo.Add("nexusa", std::make_unique<NexusABehavior>());

  SetBehavior("nexus");
}

}  // namespace nexus
}  // namespace zero
