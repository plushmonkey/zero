#include "TrenchWars.h"

#include <stdlib.h>
#include <string.h>
#include <zero/BotController.h>
#include <zero/Utility.h>
#include <zero/ZeroBot.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/zones/ZoneController.h>
#include <zero/zones/trenchwars/SharkBehavior.h>
#include <zero/zones/trenchwars/SpiderBehavior.h>
#include <zero/zones/trenchwars/TerrierBehavior.h>

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
  repo.Add("terrier", std::make_unique<TerrierBehavior>());
  repo.Add("shark", std::make_unique<SharkBehavior>());

  SetBehavior("spider");
}

void TwController::CreateFlagroomBitset() {
  auto& map = bot->game->GetMap();
  auto& pathfinder = *bot->bot_controller->pathfinder;

  const AnimatedTileSet& flag_tiles = map.GetAnimatedTileSet(AnimatedTile::Flag);
  if (flag_tiles.count != 1 && flag_tiles.count != 3) {
    Log(LogLevel::Warning, "TrenchWars: ZoneController being used without valid flag position.");
    return;
  }

  Vector2f center_flag_avg;
  for (size_t i = 0; i < flag_tiles.count; ++i) {
    center_flag_avg += Vector2f((float)flag_tiles.tiles[i].x, (float)flag_tiles.tiles[i].y);
  }

  center_flag_avg *= (1.0f / flag_tiles.count);

  s32 flag_x = (s32)center_flag_avg.x;
  s32 flag_y = (s32)center_flag_avg.y;

  Log(LogLevel::Debug, "TrenchWars: Building flag room region.");

  auto tw = trench_wars.get();

  tw->flag_position = Vector2f((float)flag_x, (float)flag_y);
  tw->entrance_position = tw->flag_position + Vector2f(0, 12);

  bot->execute_ctx.blackboard.Set("tw_flag_position", tw->flag_position);
  bot->execute_ctx.blackboard.Set("tw_entrance_position", tw->entrance_position);

  auto visit = [tw](MapCoord coord) {
    tw->fr_bitset.Set(coord.x, coord.y);
#if TW_RENDER_FR
    tw->fr_positions.emplace_back((float)coord.x, (float)coord.y);
#endif
  };
  auto visited = [tw](MapCoord coord) { return tw->fr_bitset.Test(coord.x, coord.y); };

  auto get_door_corridor_size = [map](MapCoord coord, s16 horizontal, s16 vertical) {
    size_t first_area = 0;
    for (size_t i = 0; i < 5; ++i) {
      s16 x_offset = (s16)(i + 1) * horizontal;
      s16 y_offset = (s16)(i + 1) * vertical;

      if (!map.IsDoor(coord.x - x_offset, coord.y - y_offset)) break;

      ++first_area;
    }

    size_t second_area = 0;
    for (size_t i = 0; i < 5; ++i) {
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

  enum class CorridorType { None, Vertical, Horizontal };
  auto get_corridor_type = [map, get_empty_corridor_size, get_door_corridor_size](MapCoord coord,
                                                                                  bool center) -> CorridorType {
    bool door_tile = map.IsDoor(coord.x, coord.y);

    if (door_tile) {
      size_t horizontal_size = get_door_corridor_size(coord, 1, 0);
      size_t vertical_size = get_door_corridor_size(coord, 0, 1);

      if (horizontal_size == 3 || horizontal_size == 5) {
        return CorridorType::Horizontal;
      }

      if (vertical_size == 3 || vertical_size == 5) {
        return CorridorType::Vertical;
      }

      if (horizontal_size == 1 && vertical_size == 1) {
        return CorridorType::Horizontal;
      }

      return CorridorType::None;
    }

    s32 size_check = 2 + (s32)center;
    size_t empty_hsize = get_empty_corridor_size(coord, 1, 0);
    size_t empty_vsize = get_empty_corridor_size(coord, 0, 1);

    if (empty_hsize <= size_check) {
      return CorridorType::Horizontal;
    }

    if (empty_vsize <= size_check) {
      return CorridorType::Vertical;
    }

    return CorridorType::None;
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
  constexpr s32 kCorridorInclusion = 6;

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
    CorridorType corridor_type = get_corridor_type(coord, center);

    if (corridor_type != CorridorType::None) {
      if (!center && corridor_type == CorridorType::Vertical) {
        Vector2f corridor((float)coord.x, (float)coord.y);

        // Push this corridor 1 horizontally toward flag so it's within the flagroom.
        corridor.x += (coord.x < flag_x) ? 1 : -1;

        bool is_new_corridor = true;
        for (size_t i = 0; i < tw->corridors.size(); ++i) {
          if (tw->corridors[i].DistanceSq(corridor) < 5.0f * 5.0f) {
            is_new_corridor = false;
            break;
          }
        }

        if (is_new_corridor) {
          tw->corridors.push_back(corridor);
        }
      }

      current.depth = kMaxFloodDistance - kCorridorInclusion;
    }

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
  Log(LogLevel::Debug, "TrenchWars: Flag room position count: %zu", tw->fr_positions.size());
#endif

  tw->left_entrance_path =
      pathfinder.FindPath(map, tw->entrance_position, tw->flag_position + Vector2f(-1, 0), kShipRadius, 0xFFFF);
  tw->right_entrance_path =
      pathfinder.FindPath(map, tw->entrance_position, tw->flag_position + Vector2f(1, 0), kShipRadius, 0xFFFF);

  if (tw->left_entrance_path.Empty() || tw->right_entrance_path.Empty()) {
    Log(LogLevel::Warning, "TrenchWars: Failed to generate entrance paths.");
  }

  Log(LogLevel::Debug, "TrenchWars: Done building flag room.");
}

}  // namespace tw
}  // namespace zero
