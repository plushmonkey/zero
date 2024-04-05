#include <zero/BotController.h>
#include <zero/Utility.h>
#include <zero/ZeroBot.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/zones/ZoneController.h>
#include <zero/zones/devastation/BaseManager.h>
#include <zero/zones/devastation/base/TestBehavior.h>
#include <zero/zones/devastation/center/CenterBehavior.h>

namespace zero {
namespace deva {

struct DevastationController : ZoneController {
  std::unique_ptr<BaseManager> base_manager;

  bool IsZone(Zone zone) override {
    bot->execute_ctx.blackboard.Erase("base_manager");
    base_manager = nullptr;
    return zone == Zone::Devastation;
  }

  void CreateBehaviors(const char* arena_name) override;
};

static DevastationController controller;

void DevastationController::CreateBehaviors(const char* arena_name) {
  // Create behaviors depending on arena name
  if (isdigit(arena_name[0])) {
    Log(LogLevel::Info, "Registering Devastation behaviors for public arena.");

    base_manager = std::make_unique<BaseManager>(*this->bot);

    auto& repo = bot->bot_controller->behaviors;

    repo.Add("center", std::make_unique<CenterBehavior>());
    repo.Add("test", std::make_unique<TestBehavior>());

    SetBehavior("center");

    bot->execute_ctx.blackboard.Set("base_manager", base_manager.get());
    bot->bot_controller->energy_tracker.estimate_type = EnergyHeuristicType::Maximum;
  } else {
    Log(LogLevel::Info, "No Devastation behaviors defined for arena '%s'.", arena_name);
  }
}

}  // namespace deva
}  // namespace zero
