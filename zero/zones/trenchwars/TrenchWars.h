#pragma once

#include <zero/Math.h>
#include <zero/RegionRegistry.h>
#include <zero/game/GameEvent.h>
#include <zero/path/Path.h>

#include <bitset>
#include <memory>
#include <vector>

#define TW_RENDER_FR 0

namespace zero {

struct ZeroBot;

namespace tw {

struct RegionBitset {
  std::bitset<1024 * 1024> data;

  inline bool Test(Vector2f position) const { return Test((u16)position.x, (u16)position.y); }

  inline bool Test(u16 x, u16 y) const {
    if (x > 1023 || y > 1023) return false;
    return data.test((size_t)y * (size_t)1024 + (size_t)x);
  }

  inline void Set(Vector2f position, bool val = true) { Set((u16)position.x, (u16)position.y, val); }

  inline void Set(u16 x, u16 y, bool val = true) {
    if (x > 1023 || y > 1023) return;
    data.set((size_t)y * (size_t)1024 + (size_t)x, val);
  }

  inline void Clear() { data.reset(); }
};

enum class Sector {
  Flagroom,
  Entrance,
  Middle,
  Bottom,
  West,    // At least as high as middle, but in the west tube.
  East,    // At least as high as middle, but in the east tube.
  Roof,    // Outside of the base but higher than flagroom.
  Center,  // Outside of the base but not above roof.
};

struct TrenchWars : EventHandler<DoorToggleEvent> {
  ZeroBot& bot;

  // Bitset representing every tile that is considered part of the flagroom.
  RegionBitset fr_bitset;
  // Bitset representing every tile that is considered part of the flagroom entrance.
  RegionBitset entrance_bitset;
  // Bitset representing every tile that is considered part of the middle of the base.
  RegionBitset middle_bitset;
  // Bitset representing every tile that is considered part of the entire base.
  RegionBitset base_bitset;

  // The average position of the flags, which should be the middle flag.
  Vector2f flag_position;
  // The middle corridor leading into the flagroom. This is used for some behavior checks, such as pathfinding from it
  // to find safe paths through the flagroom.
  Vector2f entrance_position;

  path::Path left_entrance_path;
  path::Path right_entrance_path;

  std::vector<Vector2f> corridors;

  // This is the list of tiles that are above the flagroom.
  // These tiles depend on the doorstate to determine if they are part of flagroom or roof.
  // This is used to update the fr_bitset when doors change.
  std::vector<MapCoord> roof_fr_set;

#if TW_RENDER_FR
  std::vector<Vector2f> fr_positions;
#endif

  // This is the most common y position of rays shot up at the bottom of the base.
  // This isn't perfect for the base bottom, but it simplifies certain actions, such as base flooding.
  u16 base_bottom_y = 0;
  // This is the highest y value of any flagroom coord. Should be the side areas of the entrance shaft in normal map.
  u16 fr_bottom_y = 0;
  // This is the highest y value of any middle area coord.
  u16 middle_bottom_y = 0;

  TrenchWars(ZeroBot& bot) : bot(bot) {}

  void Build(ZeroBot& bot);

  // This will determine which part of the map the position resides in.
  // Entrance and Flagroom can overlap, but this will prioritize returning Entrance.
  // Use InFlagroom(position) to determine flagroom presence.
  Sector GetSector(Vector2f position) const;
  bool InFlagroom(Vector2f position) const;

  void HandleEvent(const DoorToggleEvent&) override;

  static inline const char* SpawnExecuteCooldownKey() { return "tw_spawn_execute_cooldown"; }

 private:
  void BuildFlagroom(ZeroBot& bot);
  void BuildFlagroomEntrance(ZeroBot& bot);
  void BuildMiddle(ZeroBot& bot);
  void BuildBase(ZeroBot& bot);

  u16 CalculateBaseBottom(ZeroBot& bot) const;
};

}  // namespace tw
}  // namespace zero
