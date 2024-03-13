#include <zero/BotController.h>
#include <zero/Utility.h>
#include <zero/ZeroBot.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/zones/ZoneController.h>
#include <zero/zones/metalgear/GunnerBehavior.h>
#include <zero/zones/metalgear/JuggBehavior.h>
#include <zero/zones/metalgear/SniperBehavior.h>

namespace zero {
namespace mg {

struct MetalGearController : ZoneController, public EventHandler<PlayerDeathEvent> {
  bool IsZone(Zone zone) override { return zone == Zone::MetalGear; }

  void CreateBehaviors(const char* arena_name) override;

  void HandleEvent(const PlayerDeathEvent& event) override {
    auto self = this->bot->game->player_manager.GetSelf();

    if (!self) return;

    if (event.player.id == self->id || event.killer.id == self->id) {
      Log(LogLevel::Info, "%s (%d) killed by %s (%d)", event.player.name, event.bounty, event.killer.name,
          event.killer.bounty);
    }
  }
};

static MetalGearController controller;

void MetalGearController::CreateBehaviors(const char* arena_name) {
  Log(LogLevel::Info, "Registering mg behaviors.");

  auto& repo = bot->bot_controller->behaviors;

  repo.Add("jugg", std::make_unique<JuggBehavior>());
  repo.Add("sniper", std::make_unique<SniperBehavior>());
  repo.Add("gunner", std::make_unique<GunnerBehavior>());

  SetBehavior("jugg");
}

}  // namespace mg
}  // namespace zero
