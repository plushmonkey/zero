#include "BaseManager.h"

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/game/Logger.h>

namespace zero {
namespace deva {

void BaseManager::HandleEvent(const RegionBuildEvent& event) {
  base_points.clear();
}

void BaseManager::HandleEvent(const RegionTileAddEvent& event) {
  Rectangle center_rect = Rectangle::FromPositionRadius(Vector2f(512, 512), 64.0f);

  Vector2f coord_center(event.coord.x + 0.5f, event.coord.y + 0.5f);

  if (center_rect.Contains(coord_center)) return;

  auto& map = bot.bot_controller->game.GetMap();

  if (map.GetTileId(event.coord.x, event.coord.y) == kTileSafeId) {
    MapCoord surrounding[] = {
        MapCoord(event.coord.x - 1, event.coord.y),
        MapCoord(event.coord.x + 1, event.coord.y),
        MapCoord(event.coord.x, event.coord.y - 1),
        MapCoord(event.coord.x, event.coord.y + 1),
    };

    // Check if there are safe tiles around this coord and mark it as this region's base spawn point.
    for (MapCoord check : surrounding) {
      if (map.GetTileId(check.x, check.y) != kTileSafeId) {
        return;
      }
    }

    auto iter = base_points.find(event.region_index);

    if (iter == base_points.end()) {
      BasePoints points = {event.coord, {}};
      base_points[event.region_index] = points;
    } else {
      if (iter->second.second.x != 0 || iter->second.second.y != 0) {
        Log(LogLevel::Warning, "Found too many base spawns at %d, %d", (s32)event.coord.x, (s32)event.coord.y);
      }

      iter->second.second = event.coord;
    }
  }
}

void BaseManager::UpdateActiveBase(const Vector2f& position) {
  RegionIndex region = bot.bot_controller->region_registry->GetRegionIndex(position);

  auto opt_spawns = GetSpawns(region);
  if (!opt_spawns) {
    current_base = BaseInfo();
    Log(LogLevel::Debug, "No spawn set for this region.");
    return;
  }

  Vector2f first(opt_spawns->first.x, opt_spawns->first.y);
  Vector2f second(opt_spawns->second.x, opt_spawns->second.y);

  if (position.DistanceSq(first) < position.DistanceSq(second)) {
    current_base.spawn = opt_spawns->first;
    current_base.enemy_spawn = opt_spawns->second;
  } else {
    current_base.spawn = opt_spawns->second;
    current_base.enemy_spawn = opt_spawns->first;
  }

  current_base.region = region;

  Log(LogLevel::Debug, "Set current spawn to %d, %d", (s32)current_base.spawn.x, (s32)current_base.spawn.y);
}

void BaseManager::HandleEvent(const TeleportEvent& event) {
  UpdateActiveBase(event.self.position);
}

void BaseManager::HandleEvent(const PlayerAttachEvent& event) {
  // Skip setting the active base if we were already part of the game.
  if (current_base.region != kUndefinedRegion) return;
  if (event.attacher.id != bot.game->player_manager.player_id) return;

  // This could set the base up backwards if closer to the enemy safe on attach.
  // It should fix itself on next match teleport.
  // TODO: This could be fixed by performing pathfinding from new position to one of the safes, and another pathfind
  // request from an enemy to the same safe. If the enemy path is shorter then that safe is most likely the enemy's.
  UpdateActiveBase(event.destination.position);
}

void BaseManager::HandleEvent(const PlayerFreqAndShipChangeEvent& event) {
  if (event.player.id != bot.game->player_manager.player_id) return;

  // Clear our active base if we go to or from spec.
  if (event.new_ship >= 8 || event.old_ship >= 8) {
    current_base = BaseInfo();
  }

  // Clear our active base if we switch frequencies.
  if (event.old_frequency != event.new_frequency) {
    current_base = BaseInfo();
  }
}

}  // namespace deva
}  // namespace zero
