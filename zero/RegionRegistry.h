#pragma once

#include <zero/Event.h>
#include <zero/Hash.h>
#include <zero/Math.h>
#include <zero/game/Map.h>

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <unordered_map>

namespace zero {

using RegionIndex = u32;

constexpr static RegionIndex kUndefinedRegion = -1;

struct MapCoord {
  uint16_t x;
  uint16_t y;

  MapCoord() : x(0), y(0) {}
  MapCoord(uint16_t x, uint16_t y) : x(x), y(y) {}
  MapCoord(Vector2f vec) : x((uint16_t)vec.x), y((uint16_t)vec.y) {}
  Vector2f ToVector() const { return Vector2f((float)x, (float)y); }

  bool operator==(const MapCoord& other) const { return x == other.x && y == other.y; }
};

}  // namespace zero

MAKE_HASHABLE(zero::MapCoord, t.x, t.y);

namespace zero {

struct RegionBuildEvent : public Event {};
struct RegionTileAddEvent : public Event {
  MapCoord coord;
  RegionIndex region_index;

  RegionTileAddEvent(RegionIndex region_index, MapCoord coord) : region_index(region_index), coord(coord) {}
};

struct SharedRegionOwnership {
  static constexpr size_t kMaxOwners = 4;

  RegionIndex owners[kMaxOwners];
  u8 count;

  SharedRegionOwnership() : count(0) {}

  bool AddOwner(RegionIndex index) {
    if (count >= kMaxOwners) return false;
    if (HasOwner(index)) return false;

    owners[count++] = index;

    return true;
  }

  bool HasOwner(RegionIndex index) const {
    for (size_t i = 0; i < count; ++i) {
      if (owners[i] == index) {
        return true;
      }
    }

    return false;
  }
};

struct RegionFiller {
 public:
  RegionFiller(const Map& map, float radius, RegionIndex* coord_regions);

  void Fill(RegionIndex index, const MapCoord& coord) {
    this->region_index = index;

    FillEmpty(coord);
    FillSolid();

    highest_coord = MapCoord(9999, 9999);
  }

 private:
  void FillEmpty(const MapCoord& coord);
  void TraverseEmpty(const Vector2f& from, MapCoord to);

  void FillSolid();
  void TraverseSolid(const Vector2f& from, MapCoord to);

  bool IsEmptyBaseTile(const Vector2f& position) const;

  const Map& map;
  RegionIndex region_index;
  float radius;

  RegionIndex* coord_regions;
  int* region_tile_counts;

  MapCoord highest_coord;

  std::vector<RegionIndex> potential_edges;

  std::vector<MapCoord> stack;
};

class RegionRegistry {
 public:
  RegionRegistry() : region_count_(0) { memset(coord_regions_, 0xFF, sizeof(coord_regions_)); }

  bool IsConnected(MapCoord a, MapCoord b) const;
  void CreateAll(const Map& map, float radius);

  RegionIndex GetRegionIndex(MapCoord coord) const;

 private:
  bool IsRegistered(MapCoord coord) const;
  void Insert(MapCoord coord, RegionIndex index);

  RegionIndex CreateRegion();

  RegionIndex region_count_;

  RegionIndex coord_regions_[1024 * 1024];
};
}  // namespace zero
