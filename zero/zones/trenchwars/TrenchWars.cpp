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

  s32 flag_x = flag_tiles.tiles[0].x;
  s32 flag_y = flag_tiles.tiles[0].y;

  Log(LogLevel::Debug, "Building Trench Wars flag room region.");

  auto tw = trench_wars.get();

  tw->flag_position = Vector2f((float)flag_x, (float)flag_y);
  bot->execute_ctx.blackboard.Set("tw_flag_position", tw->flag_position);

  auto visit = [tw](MapCoord coord) { tw->fr_bitset.Set(coord.x, coord.y); };
  auto visited = [tw](MapCoord coord) { return tw->fr_bitset.Test(coord.x, coord.y); };

  struct VisitState {
    MapCoord coord;
    int depth;

    VisitState(MapCoord coord, int depth) : coord(coord), depth(depth) {}
  };

  std::deque<VisitState> stack;

  s32 flood_x = bot->config->GetInt("TrenchWars", "FlagroomFloodX").value_or(512);
  s32 flood_y = bot->config->GetInt("TrenchWars", "FlagroomFloodY").value_or(257);

  const char* kDefaultEntranceTiles = "478,255;478,256;478,257;546,255;546,256;546,257;511,277;512,277;513,277";

  const char* entrance_tile_string =
      bot->config->GetString("TrenchWars", "FlagroomEntranceTiles").value_or(kDefaultEntranceTiles);

  std::vector<std::string_view> entrance_tile_strings = SplitString(entrance_tile_string, ";");

  for (std::string_view tile_str : entrance_tile_strings) {
    tile_str = Trim(tile_str);
    size_t split_index = tile_str.find(',');

    if (split_index == std::string_view::npos) continue;
    ++split_index;

    size_t remaining_size = tile_str.size() - split_index;

    // This should only have up to 4 characters for the tile 0-1023
    if (remaining_size > 4) continue;

    char temp[5] = {};
    memcpy(temp, tile_str.data() + split_index, remaining_size);

    // This is safe to do since we know it will stop parsing at the ','.
    u16 x = (u16)strtol(tile_str.data(), nullptr, 10);
    u16 y = (u16)strtol(temp, nullptr, 10);

    visit(MapCoord(x, y));
  }

  stack.emplace_back(MapCoord(flood_x, flood_y), 0);

  visit(stack.front().coord);

  constexpr float kShipRadius = 0.85f;
  constexpr s32 kMaxFloodDistance = 100;

  while (!stack.empty()) {
    VisitState current = stack.front();
    MapCoord coord = current.coord;

    stack.pop_front();

    if (current.depth >= kMaxFloodDistance) continue;
    if (map.IsSolid(Vector2f(coord.x, coord.y), 0xFFFF)) continue;

    MapCoord west(coord.x - 1, coord.y);
    MapCoord east(coord.x + 1, coord.y);
    MapCoord north(coord.x, coord.y - 1);
    MapCoord south(coord.x, coord.y + 1);

    path::EdgeSet edgeset = pathfinder.GetProcessor().FindEdges(
        pathfinder.GetProcessor().GetNode(path::NodePoint(coord.x, coord.y)), kShipRadius);

    if (edgeset.IsSet(path::CoordOffset::WestIndex()) && !visited(west)) {
      stack.emplace_back(west, current.depth + 1);
      visit(west);
    }

    if (edgeset.IsSet(path::CoordOffset::EastIndex()) && !visited(east)) {
      stack.emplace_back(east, current.depth + 1);
      visit(east);
    }

    if (edgeset.IsSet(path::CoordOffset::NorthIndex()) && !visited(north)) {
      stack.emplace_back(north, current.depth + 1);
      visit(north);
    }

    if (edgeset.IsSet(path::CoordOffset::SouthIndex()) && !visited(south)) {
      stack.emplace_back(south, current.depth + 1);
      visit(south);
    }
  }

  Log(LogLevel::Debug, "Done building flag room.");
}

}  // namespace tw
}  // namespace zero
