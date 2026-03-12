#include "HockeyZone.h"

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/game/Logger.h>
#include <zero/zones/ZoneController.h>
#include <zero/zones/hockey/OffenseBehavior.h>
#include <zero/zones/hockey/GoalieBehavior.h>

#include <memory>

namespace zero {
namespace hz {

struct HockeyZoneController : ZoneController {
  bool IsZone(Zone zone) override {
    hz = nullptr;
    return zone == Zone::HockeyZone;
  }

  void CreateBehaviors(const char* arena_name) override;

  std::unique_ptr<HockeyZone> hz;
};

static HockeyZoneController controller;

void HockeyZoneController::CreateBehaviors(const char* arena_name) {
  Log(LogLevel::Info, "Registering HockeyZone behaviors.");

  hz = std::make_unique<HockeyZone>(this->bot);
  hz->Build();

  bot->execute_ctx.blackboard.Set("hz", hz.get());

  auto& repo = bot->bot_controller->behaviors;

  repo.Add("offense", std::make_unique<OffenseBehavior>());
  repo.Add("goalie", std::make_unique<GoalieBehavior>());

  SetBehavior("offense");

  bot->bot_controller->energy_tracker.estimate_type = EnergyHeuristicType::Average;
}

static Rectangle* GetGoalRect(Rink* rink, Map& map, Vector2f position) {
  // Cast a ray east to see if we hit a wall. If we did then this must be the east goal.
  CastResult result = map.Cast(position, Vector2f(1, 0), 20.0f, 0xFFFF);
  if (result.hit) {
    return &rink->east_goal_rect;
  }

  return &rink->west_goal_rect;
}

static void AddTileToGoalRect(Rectangle* rect, u16 x, u16 y) {
  Vector2f top_left((float)x, (float)y);
  Vector2f bottom_right((float)x + 1, (float)y + 1);

  if (top_left.x < rect->min.x) {
    rect->min.x = top_left.x;
  }

  if (top_left.y < rect->min.y) {
    rect->min.y = top_left.y;
  }

  if (bottom_right.x > rect->max.x) {
    rect->max.x = bottom_right.x;
  }

  if (bottom_right.y > rect->max.y) {
    rect->max.y = bottom_right.y;
  }
}

void HockeyZone::Build() {
  Map& map = bot->game->GetMap();

  const AnimatedTileSet& goal_set = map.GetAnimatedTileSet(AnimatedTile::Goal);
  for (size_t i = 0; i < goal_set.count; ++i) {
    Tile tile = goal_set.tiles[i];
    RegionIndex region_index = bot->bot_controller->region_registry->GetRegionIndex(MapCoord(tile.x, tile.y));

    if (region_index == kUndefinedRegion) continue;

    Rink* rink = GetRinkFromRegionIndex(region_index);

    if (rink == nullptr) {
      rinks.emplace_back(region_index);

      rink = GetRinkFromRegionIndex(region_index);
    }

    assert(rink);

    Vector2f position((float)tile.x + 0.5f, (float)tile.y + 0.5f);
    Rectangle* rect = GetGoalRect(rink, map, position);

    AddTileToGoalRect(rect, tile.x, tile.y);
  }

  for (auto& rink : rinks) {
    rink.center =
        (rink.east_goal_rect.min + rink.east_goal_rect.max + rink.west_goal_rect.min + rink.west_goal_rect.max) * 0.25f;

    // Loop over all doors to set the rink's door positions.
    for (size_t i = 0; i < map.door_count; ++i) {
      RegionIndex door_region =
          bot->bot_controller->region_registry->GetRegionIndex(MapCoord(map.doors[i].x, map.doors[i].y));

      if (door_region != rink.region_index) continue;

      if (rink.east_door_x == 0 && (float)map.doors[i].x < rink.center.x) {
        rink.east_door_x = map.doors[i].x;
      }

      if (rink.west_door_x == 0 && (float)map.doors[i].x > rink.center.x) {
        rink.west_door_x = map.doors[i].x;
      }

      if (rink.east_door_x && rink.west_door_x) break;
    }
  }

  Log(LogLevel::Debug, "Rink count: %zu", rinks.size());

  for (auto& rink : rinks) {
    Log(LogLevel::Debug, "East: %f, %f > %f, %f", rink.east_goal_rect.min.x, rink.east_goal_rect.min.y,
        rink.east_goal_rect.max.x, rink.east_goal_rect.max.y);
    Log(LogLevel::Debug, "West: %f, %f > %f, %f\n", rink.west_goal_rect.min.x, rink.west_goal_rect.min.y,
        rink.west_goal_rect.max.x, rink.west_goal_rect.max.y);
  }
}

Rink* HockeyZone::GetRinkFromRegionIndex(RegionIndex region_index) {
  for (size_t i = 0; i < rinks.size(); ++i) {
    if (rinks[i].region_index == region_index) {
      return &rinks[i];
    }
  }

  return nullptr;
}

Rink* HockeyZone::GetRinkFromTile(u16 x, u16 y) {
  RegionIndex region_index = bot->bot_controller->region_registry->GetRegionIndex(MapCoord(x, y));

  return GetRinkFromRegionIndex(region_index);
}

}  // namespace hz
}  // namespace zero
