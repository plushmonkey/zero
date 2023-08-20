#include <zero/ZeroBot.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/zones/hyperspace/BallBehavior.h>

namespace zero {
namespace hyperspace {

struct HyperspaceController : EventHandler<ZeroBot::JoinRequestEvent>, EventHandler<ArenaNameEvent> {
  void HandleEvent(const ZeroBot::JoinRequestEvent& event) override {
    auto zone = event.server.zone;

    in_hyperspace = (zone == Zone::Hyperspace || zone == Zone::Local);
  }

  void HandleEvent(const ArenaNameEvent& event) override {
    if (!in_hyperspace) return;

    // Create behaviors depending on arena name
    if (isdigit(event.name[0])) {
      Log(LogLevel::Info, "Registering hyperspace behaviors for public arena.");
      behavior::BehaviorRepository::Get().Add("ball", std::make_unique<BallBehavior>());
    } else {
      Log(LogLevel::Info, "No hyperspace behaviors defined for arena '%s'.", event.name);
    }
  }

  bool in_hyperspace = false;
};

static HyperspaceController controller;

}  // namespace hyperspace
}  // namespace zero
