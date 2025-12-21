#include "TrenchWars.h"

#include <stdlib.h>
#include <string.h>
#include <zero/BotController.h>
#include <zero/Utility.h>
#include <zero/ZeroBot.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/zones/ZoneController.h>
#include <zero/zones/trenchwars/basing/BasingBehavior.h>
#include <zero/zones/trenchwars/solo/SoloBehavior.h>
#include <zero/zones/trenchwars/team/TeamBehavior.h>
#include <zero/zones/trenchwars/turret/TurretBehavior.h>

#include <bitset>
#include <deque>
#include <memory>

namespace zero {
namespace tw {

struct TwController : ZoneController, EventHandler<SpawnEvent> {
  bool IsZone(Zone zone) override {
    bot->execute_ctx.blackboard.Erase("tw");
    bot->execute_ctx.blackboard.Erase("tw_flag_position");
    trench_wars = nullptr;
    return zone == Zone::TrenchWars;
  }

  void CreateBehaviors(const char* arena_name) override;

  void HandleEvent(const BotController::UpdateEvent& event) override {
    if (trench_wars && scorereset_interval > 0) {
      Tick current_tick = GetCurrentTick();

      if (TICK_DIFF(current_tick, last_scorereset_tick) > scorereset_interval) {
        Event::Dispatch(ChatQueueEvent::Public("?scorereset"));
        last_scorereset_tick = current_tick;
      }
    }
  }

  void HandleEvent(const SpawnEvent& event) override {
    constexpr u32 kSpawnDelayTicks = 50;

    if (trench_wars) {
      Tick cooldown_end = MAKE_TICK(GetCurrentTick() + kSpawnDelayTicks);
      bot->execute_ctx.blackboard.Set<Tick>(TrenchWars::SpawnExecuteCooldownKey(), cooldown_end);
    }
  }

  std::unique_ptr<TrenchWars> trench_wars;

  Tick last_scorereset_tick = 0;
  s32 scorereset_interval = 0;
};

static TwController controller;

void TwController::CreateBehaviors(const char* arena_name) {
  Log(LogLevel::Info, "Registering Trench Wars behaviors.");

  bot->bot_controller->energy_tracker.estimate_type = EnergyHeuristicType::Maximum;
  trench_wars = std::make_unique<TrenchWars>(*bot);
  bot->execute_ctx.blackboard.Set("tw", trench_wars.get());

  trench_wars->BuildFlagroom(*bot);

  auto& repo = bot->bot_controller->behaviors;

  repo.Add("basing", std::make_unique<BasingBehavior>());
  repo.Add("solo", std::make_unique<SoloBehavior>());
  repo.Add("turret", std::make_unique<TurretBehavior>());
  repo.Add("team", std::make_unique<TeamBehavior>());

  SetBehavior("basing");

  last_scorereset_tick = GetCurrentTick();
  auto opt_scorereset_interval = bot->config->GetInt("TrenchWars", "ScoreresetInterval");
  if (opt_scorereset_interval) {
    scorereset_interval = *opt_scorereset_interval;
  } else {
    // Default to 1 hour if no config exists.
    scorereset_interval = 360000;
  }

  Log(LogLevel::Info, "Scorereset interval: %u", scorereset_interval);
}

void TrenchWars::BuildFlagroom(ZeroBot& bot) {
  auto& map = bot.game->GetMap();
  auto& pathfinder = *bot.bot_controller->pathfinder;

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

  auto tw = this;

  tw->flag_position = Vector2f((float)flag_x, (float)flag_y);
  tw->entrance_position = tw->flag_position + Vector2f(0, 12);

  bot.execute_ctx.blackboard.Set("tw_flag_position", tw->flag_position);
  bot.execute_ctx.blackboard.Set("tw_entrance_position", tw->entrance_position);

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
    int corridor_count;

    VisitState(MapCoord coord, int depth, int door_traverse_count, int corridor_count)
        : coord(coord), depth(depth), door_traverse_count(door_traverse_count), corridor_count(corridor_count) {}
  };

  std::deque<VisitState> stack;

  stack.emplace_back(MapCoord(flag_x, flag_y), 0, 0, 0);

  visit(stack.front().coord);

  // Mark unreachable tiles near the flag as part of the flagroom.
  // This will stop weasels from hiding in there and not being considered occupying flagroom.
  const MapCoord kSpecialTiles[] = {
      MapCoord(flag_x - 5, flag_y),
      MapCoord(flag_x - 5, flag_y + 1),
      MapCoord(flag_x + 5, flag_y),
      MapCoord(flag_x + 5, flag_y + 1),
  };

  for (auto coord : kSpecialTiles) {
    visit(coord);
  }

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

    s32 door_traverse_count = current.door_traverse_count;

    // Add the top part of the flagroom to the roof set so we can dynamically update it.
    if (door_traverse_count > 0 && fabsf((float)coord.x - (float)flag_x) < 20) {
      tw->roof_fr_set.push_back(coord);
    }

    // Cap the side entrances so they aren't included in the flag room tiles.
    if (current.corridor_count >= 4) continue;
    if (current.depth >= kMaxFloodDistance) continue;
    if (map.IsSolidEmptyDoors(coord.x, coord.y, 0xFFFF)) continue;
    if (map.IsDoor(coord.x, coord.y)) ++door_traverse_count;

    // Create the top curve
    if (door_traverse_count > 0 && coord.y < flag_y - 15) {
      s32 x_offset = (s32)fabsf((float)flag_x - (float)coord.x);
      s32 max_y_offset = 19 - ((x_offset * x_offset) / 60);

      if (flag_y - coord.y > max_y_offset) continue;
    }

    if (door_traverse_count > kMaxFlagroomDoorTraverse) continue;

    bool in_corridor_area = coord.y - flag_y > 0 && (s32)fabsf(coord.x - (float)flag_x) < 3;
    in_corridor_area |= fabsf(coord.x - (float)flag_x) > 34;

    CorridorType corridor_type = get_corridor_type(coord, in_corridor_area);

    if (corridor_type != CorridorType::None) {
      if (!in_corridor_area && corridor_type == CorridorType::Vertical) {
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
      ++current.corridor_count;
    }

    MapCoord west(coord.x - 1, coord.y);
    MapCoord east(coord.x + 1, coord.y);
    MapCoord north(coord.x, coord.y - 1);
    MapCoord south(coord.x, coord.y + 1);

    path::EdgeSet edgeset = pathfinder.GetProcessor().FindEdges(
        pathfinder.GetProcessor().GetNode(path::NodePoint(coord.x, coord.y)), kShipRadius);

    if (edgeset.IsSet(path::CoordOffset::WestIndex()) && !visited(west)) {
      stack.emplace_back(west, current.depth + 1, door_traverse_count, current.corridor_count);
      visit(west);
    }

    if (edgeset.IsSet(path::CoordOffset::EastIndex()) && !visited(east)) {
      stack.emplace_back(east, current.depth + 1, door_traverse_count, current.corridor_count);
      visit(east);
    }

    if (edgeset.IsSet(path::CoordOffset::NorthIndex()) && !visited(north)) {
      stack.emplace_back(north, current.depth + 1, door_traverse_count, current.corridor_count);
      visit(north);
    }

    if (edgeset.IsSet(path::CoordOffset::SouthIndex()) && !visited(south)) {
      stack.emplace_back(south, current.depth + 1, door_traverse_count, current.corridor_count);
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

  // Fake a door update so we get the right flagroom.
  tw->HandleEvent(DoorToggleEvent());
}

void TrenchWars::HandleEvent(const DoorToggleEvent&) {
  // Always clear path on door change in TrenchWars.
  // This improves the behaviors so they don't get stuck thinking they can go somewhere impossible when they get stuck
  // behind doors.
  this->bot.bot_controller->current_path.Clear();

  if (roof_fr_set.empty()) return;

  Log(LogLevel::Debug, "TrenchWars: Updating roof flagroom set.");

  // This is the door index for the door that determines the state of the top area.
  constexpr u8 kDoorIndex = 168 - 162;

  u8 doormode = (u8)bot.game->connection.settings.DoorMode;

  bool is_closed = doormode & (1 << kDoorIndex);

  for (MapCoord coord : this->roof_fr_set) {
    fr_bitset.Set(coord.x, coord.y, is_closed);
  }

  // If we are rendering the fr, then manually update the positions since they may have changed.
#if TW_RENDER_FR
  this->fr_positions.clear();

  for (u16 y = 0; y < 1024; ++y) {
    for (u16 x = 0; x < 1024; ++x) {
      if (fr_bitset.Test(x, y)) {
        fr_positions.emplace_back((float)x, (float)y);
      }
    }
  }
#endif
}

}  // namespace tw
}  // namespace zero
