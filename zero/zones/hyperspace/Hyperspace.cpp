#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/zones/ZoneController.h>
#include <zero/zones/hyperspace/BallBehavior.h>
#include <zero/zones/hyperspace/CenterBehavior.h>
#include <zero/zones/hyperspace/CenterJavBehavior.h>
#include <zero/zones/hyperspace/CenterLeviBehavior.h>

namespace zero {
namespace hyperspace {

struct HyperspaceController : ZoneController {
  bool IsZone(Zone zone) override { return zone == Zone::Hyperspace || zone == Zone::Local; }

  void CreateBehaviors(const char* arena_name) override {
    // Create behaviors depending on arena name
    if (isdigit(arena_name[0])) {
      Log(LogLevel::Info, "Registering hyperspace behaviors for public arena.");

      auto& repo = bot->bot_controller->behaviors;

      repo.Add("ball", std::make_unique<BallBehavior>());
      repo.Add("center-levi", std::make_unique<CenterLeviBehavior>());
      repo.Add("center-jav", std::make_unique<CenterJavBehavior>());
      repo.Add("center", std::make_unique<CenterBehavior>());

      SetBehavior("center-jav");
    } else {
      Log(LogLevel::Info, "No hyperspace behaviors defined for arena '%s'.", arena_name);
    }
  }
};

static HyperspaceController controller;

}  // namespace hyperspace
}  // namespace zero
