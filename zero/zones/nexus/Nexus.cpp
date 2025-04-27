#include <string.h>
#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/zones/ZoneController.h>
#include <zero/zones/nexus/PubOffenseBehavior.h>
#include <zero/zones/nexus/PubCoverBehavior.h>
#include <zero/zones/nexus/TestBehavior.h>

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

  repo.Add("puboffense", std::make_unique<PubOffenseBehavior>());
  repo.Add("pubcover", std::make_unique<PubCoverBehavior>());
  repo.Add("test", std::make_unique<TestBehavior>());

  SetBehavior("puboffense");
}

}  // namespace nexus
}  // namespace zero
