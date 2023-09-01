#pragma once

#include <zero/Actuator.h>
#include <zero/ChatQueue.h>
#include <zero/Steering.h>
#include <zero/behavior/Behavior.h>
#include <zero/game/Game.h>
#include <zero/game/GameEvent.h>
#include <zero/path/Pathfinder.h>

#include <memory>

namespace zero {

namespace behavior {

class BehaviorNode;
struct ExecuteContext;

}  // namespace behavior

struct BotController : EventHandler<PlayerFreqAndShipChangeEvent>, EventHandler<JoinGameEvent> {
  Game& game;

  std::unique_ptr<path::Pathfinder> pathfinder;
  std::unique_ptr<RegionRegistry> region_registry;
  std::unique_ptr<behavior::BehaviorNode> behavior_tree;
  InputState* input;

  ChatQueue chat_queue;
  behavior::BehaviorRepository behaviors;
  Steering steering;
  Actuator actuator;
  path::Path current_path;

  BotController(Game& game);

  void Update(float dt, InputState& input, behavior::ExecuteContext& execute_ctx);

  void HandleEvent(const JoinGameEvent& event) override;
  void HandleEvent(const PlayerFreqAndShipChangeEvent& event) override;

  struct UpdateEvent : public Event {
    BotController& controller;
    behavior::ExecuteContext& ctx;

    UpdateEvent(BotController& controller, behavior::ExecuteContext& ctx) : controller(controller), ctx(ctx) {}
  };
};

}  // namespace zero
