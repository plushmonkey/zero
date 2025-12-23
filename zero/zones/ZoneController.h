#pragma once

#include <zero/BotController.h>
#include <zero/ChatQueue.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/Behavior.h>
#include <zero/game/GameEvent.h>

namespace zero {

struct ZoneController : EventHandler<ZeroBot::JoinRequestEvent>,
                        EventHandler<ArenaNameEvent>,
                        EventHandler<JoinGameEvent>,
                        EventHandler<BotController::UpdateEvent>,
                        EventHandler<BehaviorChangeEvent> {
  void HandleEvent(const ZeroBot::JoinRequestEvent& event) override {
    auto zone = event.server.zone;

    bot = &event.bot;

    in_zone = IsZone(event.server.zone);
  }

  void HandleEvent(const JoinGameEvent& event) override {
    if (!in_zone) return;

    const char* group_lookups[] = {to_string(bot->server_info.zone), "General"};

    auto opt_chats = bot->config->GetString(group_lookups, ZERO_ARRAY_SIZE(group_lookups), "Chat");
    if (opt_chats) {
      std::string chat_command = std::string("?chat=") + *opt_chats;
      Event::Dispatch(ChatQueueEvent::Public(chat_command.data()));
    }

    auto opt_chat_broadcast = bot->config->GetInt(group_lookups, ZERO_ARRAY_SIZE(group_lookups), "CommandBroadcast");
    if (opt_chat_broadcast) {
      bot->commands->SetChatBroadcast(*opt_chat_broadcast);
    }
  }

  void HandleEvent(const ArenaNameEvent& event) override {
    if (!in_zone) return;

    // Assign standard variables with priority of zone name section then General if that fails.
    const char* group_lookups[] = {to_string(bot->server_info.zone), "General"};

    auto default_behavior = bot->config->GetString(group_lookups, ZERO_ARRAY_SIZE(group_lookups), "Behavior");
    auto request_ship = bot->config->GetInt(group_lookups, ZERO_ARRAY_SIZE(group_lookups), "RequestShip");
    float radius = bot->game->connection.settings.ShipSettings[0].GetRadius();

    std::string_view request_ship_override = bot->args->GetValue({"ship"});

    if (!request_ship_override.empty()) {
      int new_request_ship = (int)(request_ship_override[0] - '0');
      if (new_request_ship >= 1 && new_request_ship <= 9) {
        request_ship = std::make_optional(new_request_ship);
      }
    }

    if (request_ship && *request_ship >= 1 && *request_ship <= 8) {
      radius = bot->game->connection.settings.ShipSettings[*request_ship - 1].GetRadius();
    }

    // Update the pathfinder before we create our behaviors so we can use that data to make decisions.
    bot->bot_controller->UpdatePathfinder(radius);

    bot->commands->Reset();
    CreateBehaviors(event.name);

    std::string_view behavior_override = bot->args->GetValue({"behavior", "b"});
    if (!behavior_override.empty()) {
      default_behavior = std::make_optional(behavior_override.data());
    }

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
