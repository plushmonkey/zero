#include <zero/ZeroBot.h>
#include <zero/game/GameEvent.h>
#include <zero/zones/hyperspace/BallBehavior.h>

namespace zero {
namespace hyperspace {

struct HyperspaceInitializer : EventHandler<JoinGameEvent> {
  void HandleEvent(const JoinGameEvent& event) override {
    behavior::BehaviorRepository::Get().Add("ball", std::make_unique<BallBehavior>());
  }
};

static HyperspaceInitializer initializer;

}  // namespace hyperspace
}  // namespace zero
