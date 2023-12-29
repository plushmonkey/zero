#include "BotController.h"

#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Logger.h>

namespace zero {

BotController::BotController(Game& game) : game(game), chat_queue(game.chat) {
  this->input = nullptr;
}

void BotController::HandleEvent(const JoinGameEvent& event) {
  Log(LogLevel::Info, "Clearing bot behaviors from JoinGameEvent.");

  behaviors.Clear();
  chat_queue.Reset();
}

void BotController::HandleEvent(const PlayerFreqAndShipChangeEvent& event) {
  if (event.player.id != game.player_manager.player_id) return;
  if (event.new_ship >= 8 || event.old_ship == event.new_ship) return;

  Log(LogLevel::Info, "Creating new registry and pathfinder.");

  float radius = game.connection.settings.ShipSettings[event.new_ship].GetRadius();
  auto processor = std::make_unique<path::NodeProcessor>(game);

  region_registry = std::make_unique<RegionRegistry>();
  region_registry->CreateAll(game.GetMap(), radius);

  pathfinder = std::make_unique<path::Pathfinder>(std::move(processor), *region_registry);

  pathfinder->CreateMapWeights(game.GetMap(), radius);
}

void BotController::Update(float dt, InputState& input, behavior::ExecuteContext& execute_ctx) {
  this->input = &input;

  steering.Reset();

  Event::Dispatch(UpdateEvent(*this, execute_ctx));

  // Used to set a delay on stdout tree printing so it doesn't constantly flicker.
  static u32 last_print = 0;
  static behavior::TreePrinter tree_printer;

  constexpr u32 kPrintTickDelay = 50;

  if (behavior_tree) {
#if 0 // Switch this to toggle tree printing.
    bool should_print = TICK_GT(GetCurrentTick(), MAKE_TICK(last_print + kPrintTickDelay));
#else
    bool should_print = false;
#endif

    if (should_print) {
      // Set to true to render { and } for each composite node.
      tree_printer.render_brackets = true;

      behavior::gDebugTreePrinter = &tree_printer;
      last_print = GetCurrentTick();
    }

    behavior_tree->Execute(execute_ctx);

    if (should_print) {
      behavior::gDebugTreePrinter = nullptr;

      // Prints the tree to the FILE*. You can pass stdout or a fopen file to this.
      tree_printer.Render(stdout);
      tree_printer.Reset();
    }
  }

  actuator.Update(game, input, steering.force, steering.rotation);
  chat_queue.Update();
}

}  // namespace zero
