#include <zero/BotController.h>
#include <zero/Utility.h>
#include <zero/ZeroBot.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/zones/ZoneController.h>
#include <zero/zones/metalgear/GunnerBehavior.h>
#include <zero/zones/metalgear/JuggBehavior.h>
#include <zero/zones/metalgear/SniperBehavior.h>

#include <format>

namespace zero {
namespace mg {

struct MetalGearController : ZoneController {
  bool IsZone(Zone zone) override { return zone == Zone::MetalGear; }

  void CreateBehaviors(const char* arena_name) override;
};

static MetalGearController controller;

void MetalGearController::CreateBehaviors(const char* arena_name) {
  // Create behaviors depending on arena name
  if (isdigit(arena_name[0])) {
    Log(LogLevel::Info, "Registering mg behaviors for public arena.");

    auto& repo = bot->bot_controller->behaviors;

    repo.Add("jugg", std::make_unique<JuggBehavior>());
    repo.Add("sniper", std::make_unique<SniperBehavior>());
    repo.Add("gunner", std::make_unique<GunnerBehavior>());

    SetBehavior("jugg");
  } else {
    Log(LogLevel::Info, "No mg behaviors defined for arena '%s'.", arena_name);
  }
}

}  // namespace mg
}  // namespace zero
