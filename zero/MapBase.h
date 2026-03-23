#pragma once

#include <string.h>
#include <zero/game/Map.h>
#include <zero/path/Pathfinder.h>

#include <memory>
#include <vector>

namespace zero {

// Associate data to each tile in a region.
// Reduces memory usage by creating a rect of the tiles and using offset into that.
// Does not dynamically fit. ShrinkFit must be called once done populating.
template <typename T>
struct RegionDataMap {
  unsigned short start_x = 0;
  unsigned short start_y = 0;

  unsigned short end_x = 0;
  unsigned short end_y = 0;

  std::vector<T> data;

  RegionDataMap() : start_x(0), start_y(0), end_x(0), end_y(0) {}

  // This should be 0, 0, to 1023, 1023 to start out as the full map before populating data and calling ShrinkFit.
  RegionDataMap(unsigned short start_x, unsigned short start_y, unsigned short end_x, unsigned short end_y)
      : start_x(start_x), start_y(start_y), end_x(end_x), end_y(end_y) {
    if (end_x <= start_x || end_y <= start_y) return;

    unsigned short width = end_x - start_x + 1;
    unsigned short height = end_y - start_y + 1;

    data.resize(height * width);
  }

  T Get(unsigned short x, unsigned short y) const {
    if (data.empty()) return 0;
    if (x < start_x || x > end_x) return 0;
    if (y < start_y || y > end_y) return 0;

    unsigned short x_delta = x - start_x;
    unsigned short y_delta = y - start_y;
    unsigned short width = end_x - start_x + 1;

    size_t index = (size_t)y_delta * (size_t)width + (size_t)x_delta;
    return data[index];
  }

  T Set(unsigned short x, unsigned short y, const T& value) {
    if (data.empty()) return T{};
    if (x < start_x || x > end_x) return T{};
    if (y < start_y || y > end_y) return T{};

    unsigned short x_delta = x - start_x;
    unsigned short y_delta = y - start_y;
    unsigned short width = end_x - start_x + 1;

    size_t index = (size_t)y_delta * (size_t)width + (size_t)x_delta;

    T result = data[index];

    data[index] = value;

    return result;
  }

  void ShrinkFit(unsigned short fit_start_x, unsigned short fit_end_x, unsigned short fit_start_y,
                 unsigned short fit_end_y) {
    // Begin by making us just fit this one tile.
    unsigned short new_start_x = fit_start_x;
    unsigned short new_start_y = fit_start_y;
    unsigned short new_end_x = fit_end_x;
    unsigned short new_end_y = fit_end_y;

    if (!data.empty()) {
      // Expand to include existing set data.
      for (unsigned short check_y = start_y; check_y <= end_y; ++check_y) {
        for (unsigned short check_x = start_x; check_x <= end_x; ++check_x) {
          bool outside_region =
              check_x < new_start_x || check_x > new_end_x || check_y < new_start_y || check_y > new_end_y;

          if (Get(check_x, check_y) != T{}) {
            if (check_x < new_start_x) new_start_x = check_x;
            if (check_x > new_end_x) new_end_x = check_x;
            if (check_y < new_start_y) new_start_y = check_y;
            if (check_y > new_end_y) new_end_y = check_y;
          }
        }
      }
    }

    if (new_start_x != start_x || new_end_x != end_x || new_start_y != start_y || new_end_y != end_y) {
      size_t new_width = (size_t)(new_end_x - new_start_x + 1);
      size_t new_height = (size_t)(new_end_y - new_start_y + 1);

      size_t total_size = new_height * new_width;

      std::vector<T> new_data(total_size);

      // Loop through old set and update our new data
      if (!data.empty()) {
        for (unsigned short check_y = start_y; check_y <= end_y; ++check_y) {
          for (unsigned short check_x = start_x; check_x <= end_x; ++check_x) {
            T value = Get(check_x, check_y);
            if (value != T{}) {
              size_t x_delta = (size_t)check_x - (size_t)new_start_x;
              size_t y_delta = (size_t)check_y - (size_t)new_start_y;

              size_t total_index = (size_t)y_delta * new_width + (size_t)x_delta;

              new_data[total_index] = value;
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
};

struct MapBase {
  RegionBitset bitset;
  RegionBitset flagroom_bitset;

  // This is one of the positions near the entrance to the base for easier pathfinding to the base.
  Vector2f entrance_position;
  // This is the position that's computed to have the largest different between direct distance to spawn and path
  // distance.
  Vector2f flagroom_position;

  // Path from entrance_position (start) to flagroom_position (goal).
  path::Path path;

  RegionDataMap<u16> path_flood_map;
  u16 max_depth;
};

struct MapBuildConfig {
  // Spawn is used to determine how far away bases are compared to their path distance.
  MapCoord spawn = MapCoord(512, 512);

  // How many different bases we should try to find in the map.
  size_t base_count = 4;

  // Flood fill from the flagroom position this many tiles to determine the flagroom bitset.
  int flagroom_size = 40;

  // How many tiles should we search around the current tile to see if it has escaped the base.
  int empty_exit_range = 25;

  // If this is true, then MapBase::path_flood_map will be populated with the distance from the flagroom_position.
  bool populate_flood_map = false;
};

std::vector<MapBase> FindBases(path::Pathfinder& pathfinder, const MapBuildConfig& cfg);

}  // namespace zero
