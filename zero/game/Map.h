#ifndef ZERO_MAP_H_
#define ZERO_MAP_H_

#include <zero/Math.h>
#include <zero/Types.h>
#include <zero/game/Memory.h>
#include <zero/game/Random.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace zero {

// This uses a bitset to manage a tightly-bound region.
// This saves memory over a full map bitset.
// Manually calling Fit when the boundary is known will have better performance than calling Set many times.
// Recommended usage:
// Fit 0, 1023, 0, 1023 to fully compact the region, fit it to one of the tiles in the set with shrink.
struct RegionBitset {
  using SliceType = unsigned int;

  unsigned short start_x = 0;
  unsigned short start_y = 0;

  unsigned short end_x = 0;
  unsigned short end_y = 0;

  std::vector<SliceType> data;

  bool Test(unsigned short x, unsigned short y) const {
    if (data.empty()) return false;
    if (x < start_x || x > end_x) return false;
    if (y < start_y || y > end_y) return false;

    return _Test(x, y);
  }

  void Set(unsigned short x, unsigned short y, bool value) {
    bool shrinking = false;

    if (!value && Test(x, y)) {
      // The value is being cleared and it's in our set, so we should clear it before shrinking.
      size_t x_delta = (size_t)x - (size_t)start_x;
      size_t y_delta = (size_t)y - (size_t)start_y;

      size_t width = (size_t)end_x - (size_t)start_x + 1;
      size_t total_index = (size_t)y_delta * width + (size_t)x_delta;
      size_t slice_index = total_index / sizeof(SliceType);
      size_t bit_index = total_index % sizeof(SliceType);

      data[slice_index] &= ~(1 << bit_index);
      shrinking = true;
    }

    Fit(x, x, y, y, shrinking);

    size_t x_delta = (size_t)x - (size_t)start_x;
    size_t y_delta = (size_t)y - (size_t)start_y;
    size_t width = (size_t)end_x - (size_t)start_x + 1;
    size_t total_index = (size_t)y_delta * width + (size_t)x_delta;
    size_t slice_index = total_index / sizeof(SliceType);
    size_t bit_index = total_index % sizeof(SliceType);

    if (value) {
      data[slice_index] |= (1 << bit_index);
    } else {
      data[slice_index] &= ~(1 << bit_index);
    }
  }

  inline void Compact() {
    if (data.empty()) return;

    for (u16 y = 0; y < 1024; ++y) {
      for (u16 x = 0; x < 1024; ++x) {
        if (Test(x, y)) {
          Fit(x, y, true);
          return;
        }
      }
    }

    Fit(0, 1023, 0, 1023, false);
  }

  inline void Fit(unsigned short x, unsigned short y, bool shrink) { Fit(x, x, y, y, shrink); }
  void Fit(unsigned short fit_start_x, unsigned short fit_end_x, unsigned short fit_start_y, unsigned short fit_end_y,
           bool shrink) {
    // Begin by making us just fit this one tile.
    unsigned short new_start_x = fit_start_x;
    unsigned short new_start_y = fit_start_y;
    unsigned short new_end_x = fit_end_x;
    unsigned short new_end_y = fit_end_y;

    if (shrink && !data.empty()) {
      // Expand to include existing set data.
      for (unsigned short check_y = start_y; check_y <= end_y; ++check_y) {
        for (unsigned short check_x = start_x; check_x <= end_x; ++check_x) {
          bool outside_region =
              check_x < new_start_x || check_x > new_end_x || check_y < new_start_y || check_y > new_end_y;

          if (_Test(check_x, check_y)) {
            if (check_x < new_start_x) new_start_x = check_x;
            if (check_x > new_end_x) new_end_x = check_x;
            if (check_y < new_start_y) new_start_y = check_y;
            if (check_y > new_end_y) new_end_y = check_y;
          }
        }
      }
    } else if (!data.empty()) {
      if (start_x < new_start_x) new_start_x = start_x;
      if (end_x > new_end_x) new_end_x = end_x;
      if (start_y < new_start_y) new_start_y = start_y;
      if (end_y > new_end_y) new_end_y = end_y;
    }

    if (new_start_x != start_x || new_end_x != end_x || new_start_y != start_y || new_end_y != end_y) {
      size_t new_width = (size_t)(new_end_x - new_start_x + 1);
      size_t new_height = (size_t)(new_end_y - new_start_y + 1);

      size_t total_size = new_height * new_width;
      size_t total_slices = (total_size + (sizeof(SliceType) - 1)) / sizeof(SliceType);

      std::vector<SliceType> new_data(total_slices);

      // Loop through old set and update our new data
      if (!data.empty()) {
        for (unsigned short check_y = start_y; check_y <= end_y; ++check_y) {
          for (unsigned short check_x = start_x; check_x <= end_x; ++check_x) {
            if (_Test(check_x, check_y)) {
              size_t x_delta = (size_t)check_x - (size_t)new_start_x;
              size_t y_delta = (size_t)check_y - (size_t)new_start_y;

              size_t total_index = (size_t)y_delta * new_width + (size_t)x_delta;
              size_t slice_index = total_index / sizeof(SliceType);
              size_t bit_index = total_index % sizeof(SliceType);

              new_data[slice_index] |= (1 << bit_index);
            }
          }
        }
      }

      data = new_data;

      start_x = new_start_x;
      start_y = new_start_y;
      end_x = new_end_x;
      end_y = new_end_y;
    }
  }

 private:
  inline bool _Test(unsigned short x, unsigned short y) const {
    // Map into local space to get bit index.
    unsigned short x_delta = x - start_x;
    unsigned short y_delta = y - start_y;
    unsigned short width = end_x - start_x + 1;

    size_t total_index = (size_t)y_delta * (size_t)width + (size_t)x_delta;
    size_t slice_index = total_index / sizeof(SliceType);
    size_t bit_index = total_index % sizeof(SliceType);

    return data[slice_index] & (1 << bit_index);
  }
};

struct Tile {
  u32 x : 12;
  u32 y : 12;
  u32 id : 8;
};

struct CastResult {
  bool hit;
  float distance;
  Vector2f position;
  Vector2f normal;
};

struct ArenaSettings;
struct BrickManager;

using TileId = u8;

constexpr u32 kTileIdFlag = 170;
constexpr u32 kTileIdSafe = 171;
constexpr u32 kTileIdGoal = 172;
constexpr int kTileIdFirstDoor = 162;
constexpr int kTileIdLastDoor = 169;
constexpr u32 kTileIdWormhole = 220;

constexpr size_t kAnimatedTileCount = 7;

enum class AnimatedTile { Goal, AsteroidSmall1, AsteroidSmall2, AsteroidLarge, SpaceStation, Wormhole, Flag };
constexpr TileId kAnimatedIds[] = {172, 216, 218, 217, 219, 220, 170};
constexpr size_t kAnimatedTileSizes[] = {1, 1, 1, 2, 6, 5, 1};

static_assert(ZERO_ARRAY_SIZE(kAnimatedIds) == kAnimatedTileCount, "Must have id for each animated tile");
static_assert(ZERO_ARRAY_SIZE(kAnimatedTileSizes) == kAnimatedTileCount, "Must have tile size for each animated tile");

struct AnimatedTileSet {
  size_t index;
  size_t count;
  Tile* tiles;
};

struct OccupyRect {
  bool occupy;

  u16 start_x;
  u16 start_y;
  u16 end_x;
  u16 end_y;
};

// Reduced version of OccupyRect to remove bool so it fits more in a cache line
struct OccupiedRect {
  u32 start_x : 10;
  u32 start_y : 10;
  u32 end_x : 10;
  u32 end_y : 10;
  u32 contains_door : 1;

  bool operator==(const OccupiedRect& other) const {
    return start_x == other.start_x && start_y == other.start_y && end_x == other.end_x && end_y == other.end_y;
  }

  inline bool Contains(Vector2f position) const {
    u16 x = (u16)position.x;
    u16 y = (u16)position.y;

    return x >= start_x && x <= end_x && y >= start_y && y <= end_y;
  }
};

inline bool IsSolid(TileId id) {
  if (id == 0) return false;
  if (id >= 162 && id <= 169) return true;
  if (id < 170) return true;
  if (id == 220) return false;
  if (id >= 192 && id <= 240) return true;
  if (id >= 242 && id <= 252) return true;

  return false;
}

inline bool IsSolidEmptyDoors(TileId id) {
  if (id == 0) return false;
  if (id >= 162 && id <= 169) return false;
  if (id < 170) return true;
  if (id >= 192 && id <= 240) return true;
  if (id >= 242 && id <= 252) return true;

  return false;
}

namespace elvl {

enum {
  RegionFlag_Base = (1 << 0),
  RegionFlag_NoAntiwarp = (1 << 1),
  RegionFlag_NoWeapons = (1 << 2),
  RegionFlag_NoFlags = (1 << 3),
};
using RegionFlags = u32;

struct Region {
  std::string name;
  RegionFlags flags = 0;

  // If a tile is part of this region then it will be set in this bitset.
  // Lazily initialized so empty regions don't take up a lot of memory.
  RegionBitset tiles;

  inline void SetTile(u16 x, u16 y) { tiles.Set(x, y, 1); }

  inline bool InRegion(u16 x, u16 y) const {
    if (x > 1023 || y > 1023) return false;

    return tiles.Test(x, y);
  }
};

}  // namespace elvl

struct Map {
  bool Load(MemoryArena& arena, const char* filename);
  bool LoadFromMemory(MemoryArena& arena, const char* filename, const u8* data, size_t size);

  bool IsSolid(u16 x, u16 y, u32 frequency) const;
  bool IsSolidEmptyDoors(u16 x, u16 y, u32 frequency) const;

  // This tells us if the tile is a door in the level map, but it might currently be open.
  // Use GetTileId if current state is desired.
  bool IsDoor(u16 x, u16 y) const;

  TileId GetTileId(u16 x, u16 y) const;
  TileId GetTileId(const Vector2f& position) const;
  void SetTileId(u16 x, u16 y, TileId id);

  bool IsSolid(const Vector2f& p, u32 frequency) const { return IsSolid((u16)p.x, (u16)p.y, frequency); }

  // Returns a possible rect that creates an occupiable area that contains the tested position.
  OccupyRect GetPossibleOccupyRect(const Vector2f& position, float radius, u32 frequency) const;
  OccupyRect GetClosestOccupyRect(Vector2f position, float radius, Vector2f point) const;
  Vector2f GetOccupyCenter(const Vector2f& position, float radius, u32 frequency) const;

  // Rects must be initialized memory that can contain all possible occupy rects.
  size_t GetAllOccupiedRects(Vector2f position, float radius, u32 frequency, OccupiedRect* rects,
                             bool dynamic_doors = false) const;

  bool CanTraverse(const Vector2f& start, const Vector2f& end, float radius, u32 frequency) const;
  bool CanOverlapTile(const Vector2f& position, float radius, u32 frequency) const;
  bool CanOccupy(const Vector2f& position, float radius, u32 frequency) const;
  bool CanOccupyRadius(const Vector2f& position, float radius, u32 frequency) const;
  bool CanFit(const Vector2f& position, float radius, u32 frequency) const;

  Vector2f ResolveShipCollision(Vector2f position, float radius, u32 frequency) const;
  // Checks if a ship is currently overlapping any tiles.
  bool IsColliding(const Vector2f& position, float radius, u32 frequency) const;

  void UpdateDoors(const ArenaSettings& settings, bool force_update = false);
  void SeedDoors(u32 seed);

  u32 GetChecksum(u32 key) const;

  CastResult Cast(const Vector2f& from, const Vector2f& direction, float max_distance, u32 frequency) const;
  CastResult CastTo(const Vector2f& from, const Vector2f& to, u32 frequency) const;

  CastResult CastShip(struct Player* player, float radius, const Vector2f& to) const;

  inline AnimatedTileSet& GetAnimatedTileSet(AnimatedTile type) { return animated_tiles[(size_t)type]; }
  inline const AnimatedTileSet& GetAnimatedTileSet(AnimatedTile type) const { return animated_tiles[(size_t)type]; }

  std::vector<const elvl::Region*> GetRegions(Vector2f position) const;
  std::vector<const elvl::Region*> GetRegions(u16 x, u16 y) const;

  bool InRegion(const char* name, Vector2f position) const;
  bool InRegion(const char* name, u16 x, u16 y) const;

  const elvl::Region* GetRegionByName(const char* name) const;

  // This is a fairly expensive operation, so the results should be cached after map is loaded once.
  std::vector<Tile> GetRegionTiles(const elvl::Region& region) const;

  // This needs to be called manually to parse the regions from the data.
  // Only do this if the regions will be used because they can use a lot of memory.
  void ParseRegions();

  char filename[1024];
  u32 checksum = 0;
  VieRNG door_rng;
  u32 last_seed_tick = 0;
  u32 compressed_size = 0;
  char* data = nullptr;
  size_t data_size = 0;
  u8* tiles = nullptr;

  size_t door_count = 0;
  Tile* doors = nullptr;

  BrickManager* brick_manager = nullptr;

  AnimatedTileSet animated_tiles[kAnimatedTileCount];

  std::vector<elvl::Region> regions;
  std::unordered_map<std::string, elvl::Region*> region_map;

 private:
  size_t GetTileCount(Tile* tiles, size_t tile_count, TileId id_begin, TileId id_end);
};

}  // namespace zero

#endif
