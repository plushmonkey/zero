#pragma once

#include <zero/Actuator.h>
#include <zero/Steering.h>
#include <zero/game/Game.h>
#include <zero/path/Pathfinder.h>

#include <memory>

namespace zero {

namespace behavior {

class BehaviorNode;
struct ExecuteContext;

}  // namespace behavior

struct BotController {
  std::unique_ptr<path::Pathfinder> pathfinder;
  std::unique_ptr<RegionRegistry> region_registry;

  Steering steering;
  Actuator actuator;
  std::vector<Vector2f> current_path;

  BotController();

  Player* GetNearestTarget(Game& game, Player& self);

  void Update(float dt, Game& game, InputState& input, behavior::ExecuteContext& execute_ctx);

private:
  std::unique_ptr<behavior::BehaviorNode> behavior_tree;
};

}  // namespace zero
