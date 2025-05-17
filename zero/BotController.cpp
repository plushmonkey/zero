#include "BotController.h"

#include <zero/Utility.h>
#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Logger.h>

namespace zero {

BotController::BotController(Game& game) : game(game), chat_queue(game.chat), energy_tracker(game.player_manager) {
  this->input = nullptr;

  this->enable_dynamic_path = true;
  this->door_solid_method = path::DoorSolidMethod::Dynamic;
}

void BotController::HandleEvent(const JoinGameEvent& event) {
  Log(LogLevel::Debug, "Clearing bot behaviors from JoinGameEvent.");

  behaviors.Clear();

  // Clear the pathfinder so it will rebuild on ship change.
  pathfinder = nullptr;

  this->enable_dynamic_path = true;
  this->door_solid_method = path::DoorSolidMethod::Dynamic;
}

void BotController::HandleEvent(const PlayerEnterEvent& event) {
  if (event.player.id != game.player_manager.player_id) return;

  // Clear chat queue from the enter event so we can queue messages during join event that occurs after.
  chat_queue.Reset();

  if (event.player.ship >= 8) return;

  float radius = game.connection.settings.ShipSettings[event.player.ship].GetRadius();

  current_path.Clear();

  Log(LogLevel::Debug, "Creating pathfinder from enter event.");
  UpdatePathfinder(radius);
}

void BotController::HandleEvent(const MapLoadEvent& event) {
  // Send a request for the arena list so we can know the name of the current arena.
  game.chat.SendMessage(ChatType::Public, "?arena");
}

void BotController::HandleEvent(const PlayerFreqAndShipChangeEvent& event) {
  if (event.player.id != game.player_manager.player_id) return;
  if (event.new_ship >= 8 || event.old_ship == event.new_ship) return;

  float radius = game.connection.settings.ShipSettings[event.new_ship].GetRadius();

  current_path.Clear();

  UpdatePathfinder(radius);
}

void BotController::UpdatePathfinder(float radius) {
  if (pathfinder && pathfinder->config.ship_radius == radius) {
    pathfinder->SetDoorSolidMethod(door_solid_method);
    return;
  }

  Log(LogLevel::Info, "Creating new registry and pathfinder.");

  auto processor = std::make_unique<path::NodeProcessor>(game);

  region_registry = std::make_unique<RegionRegistry>();
  region_registry->CreateAll(game.GetMap(), radius);

  pathfinder = std::make_unique<path::Pathfinder>(std::move(processor), *region_registry);

  path::Pathfinder::WeightConfig cfg = {};

  cfg.ship_radius = radius;
  cfg.wall_distance = 5;
  cfg.weight_type = path::Pathfinder::WeightType::Exponential;

  pathfinder->CreateMapWeights(game.temp_arena, game.GetMap(), cfg);
  pathfinder->SetDoorSolidMethod(door_solid_method);
}

void BotController::HandleEvent(const DoorToggleEvent& event) {
  if (pathfinder) {
    pathfinder->GetProcessor().MarkDynamicNodes();
  }

  if (enable_dynamic_path && current_path.dynamic) {
    Log(LogLevel::Debug, "Clearing current path from door update.");
    current_path.Clear();
  }
}

void BotController::HandleEvent(const BrickTileEvent& event) {
  auto self = game.player_manager.GetSelf();
  if (!self || self->ship >= 8) return;

  if (pathfinder) {
    pathfinder->SetBrickNode(event.brick.tile.x, event.brick.tile.y, true);
  }

  s32 x = event.brick.tile.x;
  s32 y = event.brick.tile.y;

  if (event.brick.team != self->frequency && current_path.Contains(x, y)) {
    Log(LogLevel::Debug, "Clearing current path from brick drop.");
    current_path.Clear();
  }
}

void BotController::HandleEvent(const BrickTileClearEvent& event) {
  if (pathfinder) {
    pathfinder->SetBrickNode(event.brick.tile.x, event.brick.tile.y, false);
  }

  if (current_path.dynamic) {
    Log(LogLevel::Debug, "Clearing current path from brick clear.");
    current_path.Clear();
  }
}

void BotController::HandleEvent(const LoginResponseEvent& event) {
  u8 response = event.response_id;

  if (response == 0x00 || response == 0x0D) {
    auto login_args = ParseLoginArena(default_arena);

    game.connection.SendArenaLogin(8, 0, 1920, 1080, login_args.first, login_args.second.data());
  }
}

void BotController::Update(RenderContext& rc, float dt, InputState& input, behavior::ExecuteContext& execute_ctx) {
  this->input = &input;

  steering.Reset();

  execute_ctx.blackboard.Set("world_camera", game.camera);
  execute_ctx.blackboard.Set("ui_camera", game.ui_camera);

  Event::Dispatch(UpdateEvent(*this, execute_ctx));
  energy_tracker.Update();

  static behavior::TreePrinter tree_printer;

  if (behavior_tree && pathfinder) {
    bool should_print = g_Settings.debug_behavior_tree;

    if (should_print) {
      // Set to true to render { and } for each composite node.
      tree_printer.render_brackets = false;
      // Set this to false to disable rendering of the text, then enable it in your behavior tree with
      // .Child<RenderEnableTreeNode>(true)
      tree_printer.render_text = true;

      behavior::gDebugTreePrinter = &tree_printer;
    }

    behavior_tree->Execute(execute_ctx);

    if (should_print) {
      behavior::gDebugTreePrinter = nullptr;

      tree_printer.Render(rc);
      tree_printer.Reset();
    }
  }

  actuator.Update(game, input, steering.force, steering.rotation, steering.rotation_threshold);
  chat_queue.Update();
  last_input = input;
}

}  // namespace zero
