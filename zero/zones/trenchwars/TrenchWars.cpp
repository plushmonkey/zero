#include <string.h>
#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/zones/ZoneController.h>
#include <zero/zones/trenchwars/SpiderBehavior.h>

namespace zero {
namespace tw {

struct TwController : ZoneController {
  bool IsZone(Zone zone) override { return zone == Zone::TrenchWars; }

  void CreateBehaviors(const char* arena_name) override;
};

static TwController controller;

void TwController::CreateBehaviors(const char* arena_name) {
  Log(LogLevel::Info, "Registering Trench Wars behaviors.");

  bot->bot_controller->energy_tracker.estimate_type = EnergyHeuristicType::Maximum;

  auto& repo = bot->bot_controller->behaviors;

  repo.Add("spider", std::make_unique<SpiderBehavior>());

  SetBehavior("spider");
}

}  // namespace tw
}  // namespace zero
