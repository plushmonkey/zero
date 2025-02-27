#include <zero/RegionRegistry.h>
#include <zero/game/Map.h>

#include <vector>

namespace zero {

bool IsValidPosition(MapCoord coord) {
  return coord.x >= 0 && coord.x < 1024 && coord.y >= 0 && coord.y < 1024;
}

RegionFiller::RegionFiller(const Map& map, float radius, RegionIndex* coord_regions, int* region_tile_counts)
    : map(map),
      radius(radius),
      coord_regions(coord_regions),
      region_tile_counts(region_tile_counts),
      highest_coord(9999, 9999) {
  potential_edges.reserve(1024 * 1024);

  for (size_t i = 0; i < 1024 * 1024; ++i) {
    potential_edges.push_back(kUndefinedRegion);
  }
}

void RegionFiller::FillEmpty(const MapCoord& coord) {
  if (!map.CanOverlapTile(Vector2f(coord.x, coord.y), radius, 0xFFFF)) return;

  coord_regions[coord.y * 1024 + coord.x] = region_index;
  region_tile_counts[region_index]++;

  Event::Dispatch(RegionTileAddEvent(region_index, coord));

  stack.push_back(coord);

  while (!stack.empty()) {
    MapCoord current = stack.back();
    stack.pop_back();

    const MapCoord west(current.x - 1, current.y);
    const MapCoord east(current.x + 1, current.y);
    const MapCoord north(current.x, current.y - 1);
    const MapCoord south(current.x, current.y + 1);
    const Vector2f current_pos((float)current.x + 0.5f, (float)current.y + 0.5f);

    TraverseEmpty(current_pos, west);
    TraverseEmpty(current_pos, east);
    TraverseEmpty(current_pos, north);
    TraverseEmpty(current_pos, south);
  }
}

void RegionFiller::TraverseEmpty(const Vector2f& from, MapCoord to) {
  if (!IsValidPosition(to)) return;

  size_t to_index = (size_t)to.y * 1024 + to.x;

  if (!map.CanOccupyRadius(Vector2f(to.x, to.y), radius, 0xFFFF)) {
    potential_edges[to_index] = region_index;

    if (to.y < highest_coord.y) {
      highest_coord = to;
    }
  }

  if (coord_regions[to_index] == kUndefinedRegion) {
    Vector2f to_pos((float)to.x + 0.5f, (float)to.y + 0.5f);

    if (map.CanTraverse(from, to_pos, radius, 0xFFFF)) {
      coord_regions[to_index] = region_index;
      region_tile_counts[region_index]++;
      Event::Dispatch(RegionTileAddEvent(region_index, to));
      stack.push_back(to);
    }
  }
}

void RegionFiller::FillSolid() {
  MapCoord coord = highest_coord;

  if (!IsValidPosition(Vector2f(coord.x, coord.y))) return;

  stack.clear();

  stack.push_back(coord);

  while (!stack.empty()) {
    MapCoord current = stack.back();
    Vector2f current_pos((float)current.x, (float)current.y);

    stack.pop_back();

    if (IsEmptyBaseTile(current_pos)) continue;

    const MapCoord west(current.x - 1, current.y);
    const MapCoord northwest(current.x - 1, current.y - 1);
    const MapCoord southwest(current.x - 1, current.y + 1);
    const MapCoord east(current.x + 1, current.y);
    const MapCoord northeast(current.x + 1, current.y - 1);
    const MapCoord southeast(current.x + 1, current.y + 1);
    const MapCoord north(current.x, current.y - 1);
    const MapCoord south(current.x, current.y + 1);

    TraverseSolid(current_pos, west);
    TraverseSolid(current_pos, northwest);
    TraverseSolid(current_pos, southwest);
    TraverseSolid(current_pos, east);
    TraverseSolid(current_pos, northeast);
    TraverseSolid(current_pos, southeast);
    TraverseSolid(current_pos, north);
    TraverseSolid(current_pos, south);
  }
}

void RegionFiller::TraverseSolid(const Vector2f& from, MapCoord to) {
  if (!IsValidPosition(Vector2f(to.x, to.y))) return;

  size_t to_index = (size_t)to.y * 1024 + to.x;

  if (potential_edges[to_index] == region_index) {
    stack.push_back(to);
    potential_edges[to_index] = kUndefinedRegion;

#if 0
    // Add an edge if this tile is not part of the empty space within the base
    if (coord_regions[to_index] != region_index) {
      Vector2f to_pos = Vector2f((float)to.x, (float)to.y);
      if (!IsEmptyBaseTile(to_pos)) {
        edges[to_index].AddOwner(region_index);
      }
    }
#endif
  }
}

bool RegionFiller::IsEmptyBaseTile(const Vector2f& position) const {
  if (map.IsSolid(position, 0xFFFF)) return false;

  OccupyRect rect = map.GetPossibleOccupyRect(position, radius, 0xFFFF);

  if (rect.occupy) {
    size_t top_index = rect.start_y * 1024 + rect.start_x;
    size_t bottom_index = rect.end_y * 1024 + rect.end_x;

    if (coord_regions[top_index] == region_index || coord_regions[bottom_index] == region_index) {
      return true;
    }
  }

  return false;
}

void RegionRegistry::CreateAll(const Map& map, float radius) {
  Event::Dispatch(RegionBuildEvent());

  RegionFiller filler(map, radius, coord_regions_, region_tile_counts_);

  for (uint16_t y = 0; y < 1024; ++y) {
    for (uint16_t x = 0; x < 1024; ++x) {
      MapCoord coord(x, y);

      if (map.CanOverlapTile(Vector2f(x, y), radius, 0xFFFF)) {
        // If the current coord is empty and hasn't been inserted into region
        // map then create a new region and flood fill it
        if (!IsRegistered(coord)) {
          RegionIndex region_index = CreateRegion();

          filler.Fill(region_index, coord);
        }
      }
    }
  }
}

int RegionRegistry::GetTileCount(MapCoord coord) const {
  RegionIndex index = coord_regions_[coord.y * 1024 + coord.x];

  return region_tile_counts_[index];
}

bool RegionRegistry::IsRegistered(MapCoord coord) const {
  if (!IsValidPosition(coord)) return false;
  return coord_regions_[coord.y * 1024 + coord.x] != -1;
}

void RegionRegistry::Insert(MapCoord coord, RegionIndex index) {
  if (!IsValidPosition(coord)) return;
  coord_regions_[coord.y * 1024 + coord.x] = index;
}

RegionIndex RegionRegistry::CreateRegion() {
  return region_count_++;
}

RegionIndex RegionRegistry::GetRegionIndex(MapCoord coord) const {
  if (!IsValidPosition(coord)) return kUndefinedRegion;
  return coord_regions_[coord.y * 1024 + coord.x];
}

bool RegionRegistry::IsConnected(MapCoord a, MapCoord b) const {
  // Only one needs to be checked for invalid because the second line will
  if (!IsValidPosition(a)) return false;
  if (!IsValidPosition(b)) return false;

  RegionIndex first = coord_regions_[a.y * 1024 + a.x];
  if (first == -1) return false;

  RegionIndex second = coord_regions_[b.y * 1024 + b.x];

  return first == second;
}

}  // namespace zero
