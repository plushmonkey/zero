#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/Behavior.h>
#include <zero/game/GameEvent.h>

namespace zero {

struct ZoneController : EventHandler<ZeroBot::JoinRequestEvent>,
                        EventHandler<ArenaNameEvent>,
                        EventHandler<BotController::UpdateEvent>,
                        EventHandler<BehaviorChangeEvent> {
  void HandleEvent(const ZeroBot::JoinRequestEvent& event) override {
    auto zone = event.server.zone;

    bot = &event.bot;

    in_zone = IsZone(event.server.zone);
  }

  void HandleEvent(const ArenaNameEvent& event) override {
    if (!in_zone) return;

    // Assign standard variables with priority of zone name section then General if that fails.
    const char* group_lookups[] = {to_string(bot->server_info.zone), "General"};

    auto default_behavior = bot->config->GetString(group_lookups, ZERO_ARRAY_SIZE(group_lookups), "Behavior");
    auto request_ship = bot->config->GetInt(group_lookups, ZERO_ARRAY_SIZE(group_lookups), "RequestShip");

    float radius = bot->game->connection.settings.ShipSettings[0].GetRadius();

    if (request_ship && *request_ship >= 1 && *request_ship <= 8) {
      radius = bot->game->connection.settings.ShipSettings[*request_ship - 1].GetRadius();
    }

    // Update the pathfinder before we create our behaviors so we can use that data to make decisions.
    bot->bot_controller->UpdatePathfinder(radius);

    bot->commands->Reset();
    CreateBehaviors(event.name);

    if (default_behavior) {
      SetBehavior(*default_behavior);
    }

    // Set request ship after the behavior is initialized so it can override the blackboard variable.
    if (request_ship && *request_ship > 0 && *request_ship < 10) {
      bot->execute_ctx.blackboard.Set("request_ship", *request_ship - 1);
    }
  }

  virtual void HandleEvent(const BotController::UpdateEvent& event) override {}
  virtual void HandleEvent(const BehaviorChangeEvent& event) override {}

  // Return true if this controller should become active.
  virtual bool IsZone(Zone zone) = 0;

  // Map is already loaded at this point, so it's safe to read its data.
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
