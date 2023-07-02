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
constexpr u32 kTileSafeId = 171;
constexpr u32 kGoalTileId = 172;

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

struct Map {
  bool Load(MemoryArena& arena, const char* filename);

  bool IsSolid(u16 x, u16 y, u32 frequency) const;
  TileId GetTileId(u16 x, u16 y) const;
  TileId GetTileId(const Vector2f& position) const;
  void SetTileId(u16 x, u16 y, TileId id);

  bool IsSolid(const Vector2f& p, u32 frequency) const { return IsSolid((u16)p.x, (u16)p.y, frequency); }

  // Returns a possible rect that creates an occupiable area that contains the tested position.
  OccupyRect GetPossibleOccupyRect(const Vector2f& position, float radius, u32 frequency) const;
  OccupyRect GetClosestOccupyRect(Vector2f position, float radius, Vector2f point) const;
  bool CanTraverse(const Vector2f& start, const Vector2f& end, float radius, u32 frequency) const;
  bool CanOverlapTile(const Vector2f& position, float radius, u32 frequency) const;
  bool CanOccupy(const Vector2f& position, float radius, u32 frequency) const;
  bool CanOccupyRadius(const Vector2f& position, float radius, u32 frequency) const;
  bool CanFit(const Vector2f& position, float radius, u32 frequency) const;

  void UpdateDoors(const ArenaSettings& settings);
  void SeedDoors(u32 seed);

  u32 GetChecksum(u32 key) const;

  CastResult Cast(const Vector2f& from, const Vector2f& direction, float max_distance, u32 frequency) const;
  CastResult CastTo(const Vector2f& from, const Vector2f& to, u32 frequency) const;

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
