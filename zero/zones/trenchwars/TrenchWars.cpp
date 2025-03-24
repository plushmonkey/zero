#include "TrenchWars.h"

#include <stdlib.h>
#include <string.h>
#include <zero/BotController.h>
#include <zero/Utility.h>
#include <zero/ZeroBot.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/zones/ZoneController.h>
#include <zero/zones/trenchwars/SpiderBehavior.h>

#include <bitset>
#include <deque>
#include <memory>

namespace zero {
namespace tw {

struct TwController : ZoneController {
  bool IsZone(Zone zone) override {
    bot->execute_ctx.blackboard.Erase("tw");
    bot->execute_ctx.blackboard.Erase("tw_flag_position");
    trench_wars = nullptr;
    return zone == Zone::TrenchWars;
  }

  void CreateBehaviors(const char* arena_name) override;

  void CreateFlagroomBitset();

  std::unique_ptr<TrenchWars> trench_wars;
};

static TwController controller;

void TwController::CreateBehaviors(const char* arena_name) {
  Log(LogLevel::Info, "Registering Trench Wars behaviors.");

  bot->bot_controller->energy_tracker.estimate_type = EnergyHeuristicType::Maximum;
  trench_wars = std::make_unique<TrenchWars>();
  bot->execute_ctx.blackboard.Set("tw", trench_wars.get());

  CreateFlagroomBitset();

  auto& repo = bot->bot_controller->behaviors;

  repo.Add("spider", std::make_unique<SpiderBehavior>());

  SetBehavior("spider");
}

void TwController::CreateFlagroomBitset() {
  auto& map = bot->game->GetMap();
  auto& pathfinder = *bot->bot_controller->pathfinder;

  const AnimatedTileSet& flag_tiles = map.GetAnimatedTileSet(AnimatedTile::Flag);
  if (flag_tiles.count != 1 && flag_tiles.count != 3) {
    Log(LogLevel::Warning, "Trench Wars controller being used without valid flag position.");
    return;
  }

  Vector2f center_flag_avg;
  for (size_t i = 0; i < flag_tiles.count; ++i) {
    center_flag_avg += Vector2f((float)flag_tiles.tiles[i].x, (float)flag_tiles.tiles[i].y);
  }

  center_flag_avg *= (1.0f / flag_tiles.count);

  s32 flag_x = (s32)center_flag_avg.x;
  s32 flag_y = (s32)center_flag_avg.y;

  Log(LogLevel::Debug, "Building Trench Wars flag room region.");

  auto tw = trench_wars.get();

  tw->flag_position = Vector2f((float)flag_x, (float)flag_y);
  bot->execute_ctx.blackboard.Set("tw_flag_position", tw->flag_position);

  auto visit = [tw](MapCoord coord) {
    tw->fr_bitset.Set(coord.x, coord.y);
#if TW_RENDER_FR
    tw->fr_positions.emplace_back((float)coord.x, (float)coord.y);
#endif
  };
  auto visited = [tw](MapCoord coord) { return tw->fr_bitset.Test(coord.x, coord.y); };

  auto get_door_corridor_size = [map](MapCoord coord, s16 horizontal, s16 vertical) {
    size_t first_area = 0;
    for (size_t i = 0; i < 4; ++i) {
      s16 x_offset = (s16)(i + 1) * horizontal;
      s16 y_offset = (s16)(i + 1) * vertical;

      if (!map.IsDoor(coord.x - x_offset, coord.y - y_offset)) break;

      ++first_area;
    }

    size_t second_area = 0;
    for (size_t i = 0; i < 4; ++i) {
      s16 x_offset = (s16)(i + 1) * horizontal;
      s16 y_offset = (s16)(i + 1) * vertical;

      if (!map.IsDoor(coord.x + x_offset, coord.y + y_offset)) break;

      ++second_area;
    }

    return first_area + second_area + 1;
  };
  auto get_empty_corridor_size = [map](MapCoord coord, s16 horizontal, s16 vertical) {
    size_t first_area = 0;
    for (size_t i = 0; i < 3; ++i) {
      s16 x_offset = (s16)(i + 1) * horizontal;
      s16 y_offset = (s16)(i + 1) * vertical;

      if (map.IsSolidEmptyDoors(coord.x - x_offset, coord.y - y_offset, 0xFFFF)) break;
      if (!map.IsDoor(coord.x - x_offset, coord.y - y_offset)) {
        ++first_area;
      }
    }

    size_t second_area = 0;
    for (size_t i = 0; i < 3; ++i) {
      s16 x_offset = (s16)(i + 1) * horizontal;
      s16 y_offset = (s16)(i + 1) * vertical;

      if (map.IsSolidEmptyDoors(coord.x + x_offset, coord.y + y_offset, 0xFFFF)) break;
      if (!map.IsDoor(coord.x + x_offset, coord.y + y_offset)) {
        ++second_area;
      }
    }

    return 1 + first_area + second_area;
  };
  auto is_corridor = [map, get_empty_corridor_size, get_door_corridor_size](MapCoord coord, bool center) {
    bool door_tile = map.IsDoor(coord.x, coord.y);

    if (door_tile) {
      size_t horizontal_size = get_door_corridor_size(coord, 1, 0);
      size_t vertical_size = get_door_corridor_size(coord, 0, 1);

      if (horizontal_size == 3 || vertical_size == 3 || horizontal_size == 5 || vertical_size == 5) {
        return true;
      }

      return horizontal_size == 1 && vertical_size == 1;
    }

    s32 size_check = 2 + (s32)center;
    size_t empty_hsize = get_empty_corridor_size(coord, 1, 0);
    size_t empty_vsize = get_empty_corridor_size(coord, 0, 1);

    return (empty_hsize <= size_check) || (empty_vsize <= size_check);
  };

  struct VisitState {
    MapCoord coord;
    int depth;
    int door_traverse_count;

    VisitState(MapCoord coord, int depth, int door_traverse_count)
        : coord(coord), depth(depth), door_traverse_count(door_traverse_count) {}
  };

  std::deque<VisitState> stack;

  stack.emplace_back(MapCoord(flag_x, flag_y), 0, 0);

  visit(stack.front().coord);

  constexpr float kShipRadius = 0.85f;
  constexpr s32 kMaxFloodDistance = 100;
  constexpr s32 kMaxFlagroomDoorTraverse = 2;

  // Open the doors in the pathfinder so the dynamic edge sets don't consider the doors to be dynamic.
  auto old_door_method = pathfinder.GetProcessor().GetDoorSolidMethod();
  pathfinder.GetProcessor().SetDoorSolidMethod(path::DoorSolidMethod::AlwaysOpen);

  while (!stack.empty()) {
    VisitState current = stack.front();
    MapCoord coord = current.coord;

    stack.pop_front();

    if (current.depth >= kMaxFloodDistance) continue;
    if (map.IsSolidEmptyDoors(coord.x, coord.y, 0xFFFF)) continue;

    s32 door_traverse_count = current.door_traverse_count;

    if (map.IsDoor(coord.x, coord.y)) ++door_traverse_count;
    if (door_traverse_count > 0 && flag_y - coord.y > 16) continue;
    if (door_traverse_count > kMaxFlagroomDoorTraverse) continue;

    bool center = coord.y - flag_y > 0 && (s32)fabsf(coord.x - (float)flag_x) < 3;
    if (is_corridor(coord, center)) continue;

    MapCoord west(coord.x - 1, coord.y);
    MapCoord east(coord.x + 1, coord.y);
    MapCoord north(coord.x, coord.y - 1);
    MapCoord south(coord.x, coord.y + 1);

    path::EdgeSet edgeset = pathfinder.GetProcessor().FindEdges(
        pathfinder.GetProcessor().GetNode(path::NodePoint(coord.x, coord.y)), kShipRadius);

    if (edgeset.IsSet(path::CoordOffset::WestIndex()) && !visited(west)) {
      stack.emplace_back(west, current.depth + 1, door_traverse_count);
      visit(west);
    }

    if (edgeset.IsSet(path::CoordOffset::EastIndex()) && !visited(east)) {
      stack.emplace_back(east, current.depth + 1, door_traverse_count);
      visit(east);
    }

    if (edgeset.IsSet(path::CoordOffset::NorthIndex()) && !visited(north)) {
      stack.emplace_back(north, current.depth + 1, door_traverse_count);
      visit(north);
    }

    if (edgeset.IsSet(path::CoordOffset::SouthIndex()) && !visited(south)) {
      stack.emplace_back(south, current.depth + 1, door_traverse_count);
      visit(south);
    }
  }

  pathfinder.GetProcessor().SetDoorSolidMethod(old_door_method);

#if TW_RENDER_FR
  Log(LogLevel::Debug, "Flag room position count: %zu", tw->fr_positions.size());
#endif

  Log(LogLevel::Debug, "Done building flag room.");
}

}  // namespace tw
}  // namespace zero
