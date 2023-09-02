#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/Behavior.h>
#include <zero/game/GameEvent.h>

namespace zero {

struct ZoneController : EventHandler<ZeroBot::JoinRequestEvent>,
                        EventHandler<ArenaNameEvent>,
                        EventHandler<BotController::UpdateEvent> {
  void HandleEvent(const ZeroBot::JoinRequestEvent& event) override {
    auto zone = event.server.zone;

    bot = &event.bot;

    in_zone = IsZone(event.server.zone);
  }

  void HandleEvent(const ArenaNameEvent& event) override {
    if (!in_zone) return;

    bot->commands->Reset();
    CreateBehaviors(event.name);
  }

  virtual void HandleEvent(const BotController::UpdateEvent& event) override {}

  // Return true if this controller should become active.
  virtual bool IsZone(Zone zone) = 0;
  virtual void CreateBehaviors(const char* arena_name) = 0;

  void SetBehavior(const char* name) {
    if (!bot) return;

    auto& ctx = bot->execute_ctx;

    auto default_behavior = bot->bot_controller->behaviors.Find(name);

    if (default_behavior) {
      default_behavior->OnInitialize(ctx);
      bot->bot_controller->SetBehavior(name, default_behavior->CreateTree(ctx));
    }
  }

  bool in_zone = false;
  ZeroBot* bot = nullptr;
};

}  // namespace zero
