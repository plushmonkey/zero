#include <zero/game/Map.h>
#include <zero/RegionRegistry.h>

#include <vector>

namespace zero {

bool IsValidPosition(MapCoord coord) {
  return coord.x >= 0 && coord.x < 1024 && coord.y >= 0 && coord.y < 1024;
}

RegionFiller::RegionFiller(const Map& map, float radius, RegionIndex* coord_regions, SharedRegionOwnership* edges,
                           int* region_tile_counts)
    : map(map),
      radius(radius),
      coord_regions(coord_regions),
      edges(edges),
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

    // Add an edge if this tile is not part of the empty space within the base
    if (coord_regions[to_index] != region_index) {
      Vector2f to_pos = Vector2f((float)to.x, (float)to.y);

      if (!IsEmptyBaseTile(to_pos)) {
        edges[to_index].AddOwner(region_index);
      }
    }
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

#if 0

/*
This method sweeps the fill area 4 times.  For each sweep, it takes the ships radius, and pins the orientation of the ship
to each of its 4 corners, then flood fills the map.  Pinning the orientation for each sweep is important because it wont allow
the fill to jump past diagonal gaps in walls.  The downside to pinning the orientation is that it leaves a space along
wall.  The fix to fill that space is to run a pass for each corner of the ship with the orientation pinned for each pass.
*/
void RegionRegistry::FloodFillRegion(const Map& map, const MapCoord& coord, RegionIndex region_index,
  float radius) {
  FloodFillEmptyRegion(map, coord, 9000, false, false, radius);
  FloodFillEmptyRegion(map, coord, 9001, false, true, radius);
  FloodFillEmptyRegion(map, coord, 9002, true, false, radius);
  FloodFillEmptyRegion(map, coord, 9003, true, true, radius);

  for (std::size_t i = 0; i < 1024 * 1024; i++) {
    for (size_t j = 9000; j <= 9003; j++) {
      if (coord_regions_[i] == j) {
        coord_regions_[i] = region_index;
      }

      if (unordered_solids_[i] == j) {
        unordered_solids_[i] = region_index;
      }

      if (outside_edges_[i] == j) {
        outside_edges_[i] = region_index;
      }
    }
  }

  MapCoord top_tile = MapCoord(9999, 9999);

  // find a tile in the set with the lowest Y coordinate, this must be part of the outside edge
  for (uint16_t y = 0; y < 1024; ++y) {
    for (uint16_t x = 0; x < 1024; ++x) {
      if (unordered_solids_[y * 1024 + x] == region_index) {
        if (y < top_tile.y) {
          top_tile = Vector2f(x, y);
        }
      }
    }
  }
  // use the found tile to flood fill this regions outside edge
  FloodFillSolidRegion(map, top_tile, region_index);
}

void RegionRegistry::FloodFillEmptyRegion(const Map& map, const MapCoord& coord, RegionIndex region_index,
  bool right_corner_check, bool bottom_corner_check, float radius) {

  if (!map.CornerPointCheck(Vector2f(coord.x, coord.y), right_corner_check, bottom_corner_check, radius)) return;

  coord_regions_[coord.y * 1024 + coord.x] = region_index;

  std::vector<MapCoord> stack;

  stack.push_back(coord);

  while (!stack.empty()) {
    MapCoord current = stack.back();
    stack.pop_back();

    const MapCoord west(current.x - 1, current.y);
    const MapCoord east(current.x + 1, current.y);
    const MapCoord north(current.x, current.y - 1);
    const MapCoord south(current.x, current.y + 1);

    if (IsValidPosition(west)) {
      if (map.CornerPointCheck(Vector2f(west.x, west.y), right_corner_check, bottom_corner_check, radius)) {
        if (coord_regions_[west.y * 1024 + west.x] != region_index) {
          coord_regions_[west.y * 1024 + west.x] = region_index;
          stack.push_back(west);
        }
      } else if (unordered_solids_[west.y * 1024 + west.x] == kUndefinedRegion) {
        unordered_solids_[west.y * 1024 + west.x] = region_index;
      }
    }

    if (IsValidPosition(east)) {
      if (map.CornerPointCheck(Vector2f(east.x, east.y), right_corner_check, bottom_corner_check, radius)) {
        if (coord_regions_[east.y * 1024 + east.x] != region_index) {
          coord_regions_[east.y * 1024 + east.x] = region_index;
          stack.push_back(east);
        }
      } else if (unordered_solids_[east.y * 1024 + east.x] == kUndefinedRegion) {
        unordered_solids_[east.y * 1024 + east.x] = region_index;
      }
    }

    if (IsValidPosition(north)) {
      if (map.CornerPointCheck(Vector2f(north.x, north.y), right_corner_check, bottom_corner_check, radius)) {
        if (coord_regions_[north.y * 1024 + north.x] != region_index) {
          coord_regions_[north.y * 1024 + north.x] = region_index;
          stack.push_back(north);
        }
      } else if (unordered_solids_[north.y * 1024 + north.x] == kUndefinedRegion) {
        unordered_solids_[north.y * 1024 + north.x] = region_index;
      }
    }

    if (IsValidPosition(south)) {
      if (map.CornerPointCheck(Vector2f(south.x, south.y), right_corner_check, bottom_corner_check, radius)) {
        if (coord_regions_[south.y * 1024 + south.x] != region_index) {
          coord_regions_[south.y * 1024 + south.x] = region_index;
          stack.push_back(south);
        }
      } else if (unordered_solids_[south.y * 1024 + south.x] == kUndefinedRegion) {
        unordered_solids_[south.y * 1024 + south.x] = region_index;
      }
    }
  }
}

// this will only work on tiles stored by the floodfillemptyregion function, to prevent it from spreading into
// regions where the walls are shared or touching each other
void RegionRegistry::FloodFillSolidRegion(const Map& map, const MapCoord& coord, RegionIndex region_index) {

  if (!IsValidPosition(Vector2f(coord.x, coord.y))) return;

  //outside_edges_[coord] = region_index;
  outside_edges_[coord.y * 1024 + coord.x] = region_index;
  std::vector<MapCoord> stack;

  stack.push_back(coord);

  while (!stack.empty()) {
    MapCoord current = stack.back();
    stack.pop_back();

    const MapCoord west(current.x - 1, current.y);
    const MapCoord northwest(current.x - 1, current.y - 1);
    const MapCoord southwest(current.x - 1, current.y + 1);
    const MapCoord east(current.x + 1, current.y);
    const MapCoord northeast(current.x + 1, current.y - 1);
    const MapCoord southeast(current.x + 1, current.y + 1);
    const MapCoord north(current.x, current.y - 1);
    const MapCoord south(current.x, current.y + 1);

    if (IsValidPosition(Vector2f(west.x, west.y))) {
      if (unordered_solids_[west.y * 1024 + west.x] == region_index) {

        stack.push_back(west);
        unordered_solids_[west.y * 1024 + west.x] = 9999;

        if (outside_edges_[west.y * 1024 + west.x] == kUndefinedRegion && !map.CanOccupy(Vector2f(west.x, west.y), 0.8f)) {
          outside_edges_[west.y * 1024 + west.x] = region_index;
        }
      }
    }

    if (IsValidPosition(Vector2f(northwest.x, northwest.y))) {
      if (unordered_solids_[northwest.y * 1024 + northwest.x] == region_index) {

        stack.push_back(northwest);
        unordered_solids_[northwest.y * 1024 + northwest.x] = 9999;

        if (outside_edges_[northwest.y * 1024 + northwest.x] == kUndefinedRegion &&
          !map.CanOccupy(Vector2f(northwest.x, northwest.y), 0.8f)) {
          outside_edges_[northwest.y * 1024 + northwest.x] = region_index;
        }
      }
    }

    if (IsValidPosition(Vector2f(southwest.x, southwest.y))) {
      if (unordered_solids_[southwest.y * 1024 + southwest.x] == region_index) {

        stack.push_back(southwest);
        unordered_solids_[southwest.y * 1024 + southwest.x] = 9999;

        if (outside_edges_[southwest.y * 1024 + southwest.x] == kUndefinedRegion &&
          !map.CanOccupy(Vector2f(southwest.x, southwest.y), 0.8f)) {
          outside_edges_[southwest.y * 1024 + southwest.x] = region_index;
        }
      }
    }

    if (IsValidPosition(Vector2f(east.x, east.y))) {
      if (unordered_solids_[east.y * 1024 + east.x] == region_index) {

        stack.push_back(east);
        unordered_solids_[east.y * 1024 + east.x] = 9999;

        if (outside_edges_[east.y * 1024 + east.x] == kUndefinedRegion &&
          !map.CanOccupy(Vector2f(east.x, east.y), 0.8f)) {
          outside_edges_[east.y * 1024 + east.x] = region_index;
        }
      }
    }

    if (IsValidPosition(Vector2f(northeast.x, northeast.y))) {
      if (unordered_solids_[northeast.y * 1024 + northeast.x] == region_index) {

        stack.push_back(northeast);
        unordered_solids_[northeast.y * 1024 + northeast.x] = 9999;

        if (outside_edges_[northeast.y * 1024 + northeast.x] == kUndefinedRegion &&
          !map.CanOccupy(Vector2f(northeast.x, northeast.y), 0.8f)) {
          outside_edges_[northeast.y * 1024 + northeast.x] = region_index;
        }
      }
    }

    if (IsValidPosition(Vector2f(southeast.x, southeast.y))) {
      if (unordered_solids_[southeast.y * 1024 + southeast.x] == region_index) {

        stack.push_back(southeast);
        unordered_solids_[southeast.y * 1024 + southeast.x] = 9999;

        if (outside_edges_[southeast.y * 1024 + southeast.x] == kUndefinedRegion &&
          !map.CanOccupy(Vector2f(southeast.x, southeast.y), 0.8f)) {
          outside_edges_[southeast.y * 1024 + southeast.x] = region_index;
        }
      }
    }

    if (IsValidPosition(Vector2f(north.x, north.y))) {
      if (unordered_solids_[north.y * 1024 + north.x] == region_index) {

        stack.push_back(north);
        unordered_solids_[north.y * 1024 + north.x] = 9999;

        if (outside_edges_[north.y * 1024 + north.x] == kUndefinedRegion &&
          !map.CanOccupy(Vector2f(north.x, north.y), 0.8f)) {
          outside_edges_[north.y * 1024 + north.x] = region_index;
        }
      }
    }

    if (IsValidPosition(Vector2f(south.x, south.y))) {
      if (unordered_solids_[south.y * 1024 + south.x] == region_index) {

        stack.push_back(south);
        unordered_solids_[south.y * 1024 + south.x] = 9999;

        if (outside_edges_[south.y * 1024 + south.x] == kUndefinedRegion &&
          !map.CanOccupy(Vector2f(south.x, south.y), 0.8f)) {
          outside_edges_[south.y * 1024 + south.x] = region_index;
        }
      }
    }

#if 0
    if (IsValidPosition(Vector2f(east.x, east.y)) && !map.CanOccupyRadius(Vector2f(east.x, east.y), 0.8f)) {
      if (outside_edges_[east.y * 1024 + east.x] == kUndefinedRegion &&
        unordered_solids_[east.y * 1024 + east.x] == region_index) {
        outside_edges_[east.y * 1024 + east.x] = region_index;
        stack.push_back(east);
      }
    }

    if (IsValidPosition(Vector2f(south.x, south.y)) && !map.CanOccupyRadius(Vector2f(south.x, south.y), 0.8f)) {
      if (outside_edges_[south.y * 1024 + south.x] == kUndefinedRegion &&
        unordered_solids_[south.y * 1024 + south.x] == region_index) {
        outside_edges_[south.y * 1024 + south.x] = region_index;
        stack.push_back(south);
      }
    }

    if (IsValidPosition(Vector2f(southwest.x, southwest.y)) && !map.CanOccupyRadius(Vector2f(southwest.x, southwest.y), 0.8f)) {
      if (outside_edges_[southwest.y * 1024 + southwest.x] == kUndefinedRegion &&
        unordered_solids_[southwest.y * 1024 + southwest.x] == region_index) {
        outside_edges_[southwest.y * 1024 + southwest.x] = region_index;
        stack.push_back(southwest);
      }
    }

    if (IsValidPosition(Vector2f(southeast.x, southeast.y)) && !map.CanOccupyRadius(Vector2f(southeast.x, southeast.y), 0.8f)) {
      if (outside_edges_[southeast.y * 1024 + southeast.x] == kUndefinedRegion &&
        unordered_solids_[southeast.y * 1024 + southeast.x] == region_index) {
        outside_edges_[southeast.y * 1024 + southeast.x] = region_index;
        stack.push_back(southeast);
      }
    }

    if (IsValidPosition(Vector2f(north.x, north.y)) && !map.CanOccupyRadius(Vector2f(north.x, north.y), 0.8f)) {
      if (outside_edges_[north.y * 1024 + north.x] == kUndefinedRegion &&
        unordered_solids_[north.y * 1024 + north.x] == region_index) {
        outside_edges_[north.y * 1024 + north.x] = region_index;
        stack.push_back(north);
      }
    }


    if (IsValidPosition(Vector2f(northwest.x, northwest.y)) && !map.CanOccupyRadius(Vector2f(northwest.x, northwest.y), 0.8f)) {
      if (outside_edges_[northwest.y * 1024 + northwest.x] == kUndefinedRegion &&
        unordered_solids_[northwest.y * 1024 + northwest.x] == region_index) {
        outside_edges_[northwest.y * 1024 + northwest.x] = region_index;
        stack.push_back(northwest);
      }
    }

    if (IsValidPosition(Vector2f(northeast.x, northeast.y)) && !map.CanOccupyRadius(Vector2f(northeast.x, northeast.y), 0.8f)) {
      if (outside_edges_[northeast.y * 1024 + northeast.x] == kUndefinedRegion &&
        unordered_solids_[northeast.y * 1024 + northeast.x] == region_index) {
        outside_edges_[northeast.y * 1024 + northeast.x] = region_index;
        stack.push_back(northeast);
      }
    }
#endif
  }
}

void RegionRegistry::CreateRegions(const Map& map, std::vector<Vector2f> seed_points, float radius) {

  //unordered_solids_.clear();

  // use a seed point to find walls on only the desired regions
 // store tiles the ship cant fit on (needs work) to help determine the regions boundries

  for (Vector2f seed : seed_points) {
    RegionIndex index = CreateRegion();

    FloodFillRegion(map, seed, index, radius);

    MapCoord top_tile = MapCoord(9999, 9999);

    // find a tile in the set with the lowest Y coordinate, this must be part of the outside edge
    for (uint16_t y = 0; y < 1024; ++y) {
      for (uint16_t x = 0; x < 1024; ++x) {
        if (unordered_solids_[y * 1024 + x] == index) {
          if (y < top_tile.y) {
            top_tile = Vector2f(x, y);
          }
        }
      }
    }
    // use the found tile to flood fill this regions outside edge
    FloodFillSolidRegion(map, top_tile, index);
  }
}

#endif

void RegionRegistry::CreateAll(const Map& map, float radius) {
  RegionFiller filler(map, radius, coord_regions_, outside_edges_, region_tile_counts_);

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

void RegionRegistry::DebugUpdate(Vector2f position) {}

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

RegionIndex RegionRegistry::GetRegionIndex(MapCoord coord) {
  if (!IsValidPosition(coord)) return -1;
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

bool RegionRegistry::IsEdge(MapCoord coord) const {
  if (!IsValidPosition(coord)) return true;
  return outside_edges_[coord.y * 1024 + coord.x].count > 0;
}

}  // namespace zero
