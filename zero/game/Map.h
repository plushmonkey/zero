#ifndef ZERO_MAP_H_
#define ZERO_MAP_H_

#include <zero/Math.h>
#include <zero/Types.h>
#include <zero/game/Memory.h>
#include <zero/game/Random.h>

namespace zero {

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

struct Map {
  bool Load(MemoryArena& arena, const char* filename);

  bool IsSolid(u16 x, u16 y, u32 frequency) const;
  bool IsSolidEmptyDoors(u16 x, u16 y, u32 frequency) const;

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

  void UpdateDoors(const ArenaSettings& settings);
  void SeedDoors(u32 seed);

  u32 GetChecksum(u32 key) const;

  CastResult Cast(const Vector2f& from, const Vector2f& direction, float max_distance, u32 frequency) const;
  CastResult CastTo(const Vector2f& from, const Vector2f& to, u32 frequency) const;

  CastResult CastShip(struct Player* player, float radius, const Vector2f& to) const;

  inline AnimatedTileSet& GetAnimatedTileSet(AnimatedTile type) { return animated_tiles[(size_t)type]; }

  char filename[1024];
  u32 checksum = 0;
  VieRNG door_rng;
  u32 last_seed_tick = 0;
  u32 compressed_size = 0;
  char* data = nullptr;
  u8* tiles = nullptr;

  size_t door_count = 0;
  Tile* doors = nullptr;

  BrickManager* brick_manager = nullptr;

  AnimatedTileSet animated_tiles[kAnimatedTileCount];

 private:
  size_t GetTileCount(Tile* tiles, size_t tile_count, TileId id_begin, TileId id_end);
};

}  // namespace zero

#endif
