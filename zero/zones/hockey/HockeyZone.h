#pragma once

#include <zero/Math.h>
#include <zero/RegionRegistry.h>

#include <vector>

namespace zero {

struct ZeroBot;

namespace hz {

struct Rink {
  RegionIndex region_index;

  Vector2f center;

  Rectangle west_goal_rect;
  Rectangle east_goal_rect;

  u16 east_door_x = 0;
  u16 west_door_x = 0;

  Rink(RegionIndex region_index) : region_index(region_index) {
    // Setup the goal rects as invalid so they can be cropped during build.
    west_goal_rect.min.x = 1024.0f;
    west_goal_rect.min.y = 1024.0f;

    east_goal_rect.min.x = 1024.0f;
    east_goal_rect.min.y = 1024.0f;
  }
};

static inline bool IsRinkClosed(Map& map, Rink& rink) {
  TileId tile_id = map.GetTileId(rink.east_door_x, (u16)rink.center.y);
  return tile_id >= kTileIdFirstDoor && tile_id <= kTileIdLastDoor;
}

struct HockeyZone {
  ZeroBot* bot;
  std::vector<Rink> rinks;

  HockeyZone(ZeroBot* bot) : bot(bot) {}

  void Build();

  Rink* GetRinkFromRegionIndex(RegionIndex region_index);
  Rink* GetRinkFromTile(u16 x, u16 y);
  inline Rink* GetRinkFromTile(Vector2f position) { return GetRinkFromTile((u16)position.x, (u16)position.y); }
};

}  // namespace hz
}  // namespace zero
