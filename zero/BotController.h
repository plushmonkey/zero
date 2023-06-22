#pragma once

#include <zero/game/Game.h>

#include <memory>

namespace zero {

struct ShipEnforcer;
class RegionRegistry;

namespace path {

struct Pathfinder;

}  // namespace path

struct BotController {
  BotController();

  Player* GetNearestTarget(Game& game, Player& self);

  void Update(float dt, Game& game, InputState& input);

 private:
  std::unique_ptr<ShipEnforcer> ship_enforcer;
  std::unique_ptr<path::Pathfinder> pathfinder;
  std::unique_ptr<RegionRegistry> region_registry;
};

}  // namespace zero
