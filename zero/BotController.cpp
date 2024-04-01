#include "BotController.h"

#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Logger.h>

namespace zero {

BotController::BotController(Game& game) : game(game), chat_queue(game.chat) {
  this->input = nullptr;
}

void BotController::HandleEvent(const JoinGameEvent& event) {
  Log(LogLevel::Debug, "Clearing bot behaviors from JoinGameEvent.");

  behaviors.Clear();
  chat_queue.Reset();

  // Clear the pathfinder so it will rebuild on ship change.
  pathfinder = nullptr;
}

void BotController::HandleEvent(const PlayerFreqAndShipChangeEvent& event) {
  if (event.player.id != game.player_manager.player_id) return;
  if (event.new_ship >= 8 || event.old_ship == event.new_ship) return;

  float radius = game.connection.settings.ShipSettings[event.new_ship].GetRadius();

  current_path.Clear();

  if (pathfinder && pathfinder->config.ship_radius == radius) return;

  Log(LogLevel::Info, "Creating new registry and pathfinder.");

  auto processor = std::make_unique<path::NodeProcessor>(game);

  region_registry = std::make_unique<RegionRegistry>();
  region_registry->CreateAll(game.GetMap(), radius);

  pathfinder = std::make_unique<path::Pathfinder>(std::move(processor), *region_registry);

  path::Pathfinder::WeightConfig cfg = {};

  cfg.ship_radius = radius;
  cfg.frequency = event.new_frequency;
  cfg.wall_distance = 5;
  cfg.weight_type = path::Pathfinder::WeightType::Exponential;

  pathfinder->CreateMapWeights(game.temp_arena, game.GetMap(), cfg);
}

void BotController::HandleEvent(const DoorToggleEvent& event) {
  if (current_path.dynamic) {
    Log(LogLevel::Debug, "Clearing current path from door update.");
    current_path.Clear();
  }
}

void BotController::Update(RenderContext& rc, float dt, InputState& input, behavior::ExecuteContext& execute_ctx) {
  this->input = &input;

  steering.Reset();

  execute_ctx.blackboard.Set("world_camera", game.camera);
  execute_ctx.blackboard.Set("ui_camera", game.ui_camera);

  Event::Dispatch(UpdateEvent(*this, execute_ctx));

  static behavior::TreePrinter tree_printer;

  if (behavior_tree) {
    bool should_print = g_Settings.debug_behavior_tree;

    if (should_print) {
      // Set to true to render { and } for each composite node.
      tree_printer.render_brackets = false;

      behavior::gDebugTreePrinter = &tree_printer;
    }

    behavior_tree->Execute(execute_ctx);

    if (should_print) {
      behavior::gDebugTreePrinter = nullptr;

      tree_printer.Render(rc);
      tree_printer.Reset();
    }
  }

  actuator.Update(game, input, steering.force, steering.rotation);
  chat_queue.Update();
}

}  // namespace zero
